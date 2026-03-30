#!/usr/bin/env bash

# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause

# The following tests run within the Docker container and assume that the
# firewall is running and is configured per the `firewall_configuration.sh`
# script.

# The shUnit2 framework is used for setup, teardown and temporary file handling.
# For further information on shUnit2 and its execution behaviour, please refer
# to the quickstart guide available at:
# https://github.com/kward/shunit2?tab=readme-ov-file#-quickstart

#
# Constants
#

EXIT_SUCCESS=0

ERROR_NO_ECHO_RESPONSE='Did not receive echo response'
ERROR_HOST_UNREACHABLE='Did not receive destination host unreachable'
ERROR_NET_UNREACHABLE='Did not receive destination net unreachable'
ERROR_FAILED_ENABLE_PING='Could not enable ping response'
ERROR_TRANSMIT_FAILED='Failed to transmit data'
ERROR_DATA_INCORRECT='The received data is different to what was sent'
ERROR_DATA_WAS_NOT_DROPPED='Firewall traffic was not dropped'
ERROR_FAILED_TO_APPLY_RULE='Failed to apply firewall rule'
ERROR_FAILED_TO_REMOVE_RULE='Failed to remove firewall rule'
ERROR_RULE_STILL_APPLIED='Firewall rule is still applied'
ERROR_INVALID_INTERFACE_COUNT='INTERFACE_COUNT must be at least 2'

FONT_HEADER=$(printf '\033[1m\033[36m')
FONT_RED=$(printf '\033[31m')
FONT_RESET=$(printf '\033[0m')

REGEX_REACHABLE='[1-9][0-9]* received'
REGEX_HOST_UNREACHABLE='Destination Host Unreachable'
REGEX_NET_UNREACHABLE='Destination Net Unreachable'

TEMPLATE_SRC='$src_ip, $src_port, $src_subnet'
TEMPLATE_DEST='$dest_ip, $dest_port, $dest_subnet'
TEMPLATE_ACTION='$interface, $action'
TEMPLATE_JSON="{ ${TEMPLATE_SRC}, ${TEMPLATE_DEST}, ${TEMPLATE_ACTION} }"

INTERFACE_COUNT=${INTERFACE_COUNT:-0}
WEBSERVER_INTERFACE=1
FIREWALL_ACTION_DROP=2

oneTimeSetUp() {
    if [ "${INTERFACE_COUNT}" -lt 2 ]; then
        print_warning "${ERROR_INVALID_INTERFACE_COUNT}"
        exit 1
    fi

    UDP_PORT=50000
    TCP_PORT=60000

    # Pings
    #
    # Tests involving pings, by default, only send 1 ping, but additional pings
    # can be sent. Timeout values, which are in seconds, can also be adjusted
    # Note: Setting `LONG_TIMEOUT` to a value less than 7 seconds will cause
    # ICMP destination unreachable tests to fail.
    COUNT=1
    TIMEOUT=1
    LONG_TIMEOUT=7

    # Temporary files
    #
    # Temporary files are stored in shUnit's temporary directory and are
    # automatically cleaned up on exit.
    SENT="${SHUNIT_TMPDIR}/sent"
    RECEIVED="${SHUNIT_TMPDIR}/received"

    # Test data
    #
    # For tests that transfer data, random data can be generated or fixed data
    # can be supplied. Setting `USE_RANDOM_DATA` to true will generate random
    # data at a specified `SIZE_BYTES`.
    TEST_DATA='/tmp/firewall_test_data'
    USE_RANDOM_DATA=true
    SIZE_BYTES=4096

    if [ "${USE_RANDOM_DATA}" = true ]; then
        generate_test_data "${SIZE_BYTES}" "${SENT}"
    elif [ ! -f "${TEST_DATA}" ]; then
        print_warning 'Warning: The file specified in TEST_DATA does not exist'
        print_warning 'Using random data instead'
        generate_test_data "${SIZE_BYTES}" "${SENT}"
    else
        cp "${TEST_DATA}" "${SENT}"
    fi

    # Output
    #
    # Network interface and route information can be displayed on startup.
    SHOW_NETWORK_INTERFACES=false
    SHOW_ROUTES=false

    if [ "${SHOW_NETWORK_INTERFACES}" = true ]; then
        print_header 'Container network interfaces'
        ifconfig

        for iface in $(all_interfaces); do
            print_header "Interface${iface} network interfaces"
            ip netns exec "namespace${iface}" ifconfig
        done
    fi

    if [ "${SHOW_ROUTES}" = true ]; then
        print_header 'Container routes'
        ip route

        for iface in $(all_interfaces); do
            print_header "Interface${iface} network routes"
            ip netns exec "namespace${iface}" ip route
        done
    fi

    # If firewall debug messages are enabled and the messages are redirected to
    # `LOG`, setting `PRINT_LOG_ON_ERROR` to true will cause debug messages
    # to be displayed when a test fails.
    LOG='/tmp/lionsos_firewall_log'
    PRINT_LOG_ON_ERROR=true

    if [ "${PRINT_LOG_ON_ERROR}" = true ] && [ ! -f "${LOG}" ]; then
        print_warning "Log file (${LOG}) does not exist"
        print_warning "Logs will not be output on error"
        PRINT_LOG_ON_ERROR=false
    fi

    # If `SAVE_SESSION_LOG_ON_EXIT` is set to true, the entire firewall log will
    # be saved to `SESSION_LOG` on exit. Setting `CLOBBER_SESSION_LOG` to true
    # will cause the session log to be overwritten if it already exists.
    # Otherwise, the output will be appended.
    SESSION_LOG='/tmp/lionsos_firewall_session_log'
    SAVE_SESSION_LOG_ON_EXIT=true
    CLOBBER_SESSION_LOG=true

    if [ -f "${LOG}" ]; then
        if [ "${SAVE_SESSION_LOG_ON_EXIT}" = true ]; then
            printf '\n%s %s\n' 'Session log will be saved to' "'${SESSION_LOG}'"
        fi

        if [ "${CLOBBER_SESSION_LOG}" = true ]; then
            cp /dev/null "${SESSION_LOG}"
        fi
    else
        print_warning 'Session logs will not be generated'
        SAVE_SESSION_LOG_ON_EXIT=false
    fi

    # If `TEST_DEBUG` is set to true, the commands executed during a test will
    # be displayed on the console.
    TEST_DEBUG=false

    print_header "Running firewall tests across ${INTERFACE_COUNT} interfaces..."
}

# Executed before each test
#
setUp() {
    if [ "${TEST_DEBUG}" = true ]; then
        set -x
    fi
}

# Executed after each test
#
tearDown() {
    if [ "${TEST_DEBUG}" = true ]; then
        set +x
        printf '\n'
    fi

    if [ "${SAVE_SESSION_LOG_ON_EXIT}" = true ]; then
        cat "${LOG}" >> "${SESSION_LOG}"
    fi

    if [ -f "${LOG}" ]; then
        cp /dev/null "${LOG}"
    fi

    cp /dev/null "${RECEIVED}"
}

#
# Test helpers
#

print_header() {
    header=$1
    printf '\n%s%s%s\n\n' "${FONT_HEADER}" "-- ${header}" "${FONT_RESET}"
}

print_warning() {
    text=$1
    printf '%s%s%s\n' "${FONT_RED}" "${text}" "${FONT_RESET}"
}

print_info() {
    text=$1
    printf '%s\n' "- ${text}"
}

print_file() {
    header=$1
    data_file=$2

    print_header "${header}"
    cat "${data_file}"
    printf '\n'
}

print_log() {
    if [ "${PRINT_LOG_ON_ERROR}" = true ]; then
        print_file 'Firewall log' "${LOG}"
    fi
}

generate_test_data() {
    block_size=$1
    output=$2
    dd if=/dev/urandom bs="${block_size}" count=1 > "${output}" 2> /dev/null
}

#
# Internet Control Message Protocol (ICMP) tests
#
run_ping_host_test() {
    src_iface=$1
    dst_iface=$2

    ip netns exec "namespace${src_iface}" \
        ping -c "${COUNT}" -w "${TIMEOUT}" \
        "$(interface_value "INTERFACE${dst_iface}_HOST_IP")" > "${RECEIVED}" 2>&1

    if ! grep -Eq --ignore-case "${REGEX_REACHABLE}" "${RECEIVED}"; then
        fail "${ERROR_NO_ECHO_RESPONSE}"
        print_log
    fi
}

run_unreachable_host_test() {
    src_iface=$1
    dst_iface=$2

    print_info "This may take up to ${LONG_TIMEOUT} seconds..."

    ip netns exec "namespace${src_iface}" \
        ping -c "${COUNT}" -w "${LONG_TIMEOUT}" \
        "$(interface_value "INTERFACE${dst_iface}_BAD_HOST_IP")" \
        > "${RECEIVED}" 2>&1

    if ! grep -Eq --ignore-case "${REGEX_HOST_UNREACHABLE}" "${RECEIVED}"; then
        fail "${ERROR_HOST_UNREACHABLE}"
        print_log
    fi
}

run_unreachable_net_test() {
    src_iface=$1
    dst_iface=$2

    ip netns exec "namespace${src_iface}" \
        ping -c "${COUNT}" -w "${LONG_TIMEOUT}" \
        "$(interface_value "INTERFACE${dst_iface}_BAD_NET_IP")" \
        > "${RECEIVED}" 2>&1

    if ! grep -Eq --ignore-case "${REGEX_NET_UNREACHABLE}" "${RECEIVED}"; then
        fail "${ERROR_NET_UNREACHABLE}"
        print_log
    fi
}

run_firewall_ping_test() {
    iface=$1

    # Ensure ping responsiveness is turned on
    response=$(api_request POST "/api/ping/${iface}/1") || {
        fail "${ERROR_FAILED_ENABLE_PING}"
        print_log
        return
    }

    # Check if the response contains an error
    if printf '%s' "${response}" | grep -q '"error"'; then
        fail "${ERROR_FAILED_ENABLE_PING}"
        print_log
        return
    fi

    ip netns exec "namespace${iface}" \
        ping -c "${COUNT}" -w "${TIMEOUT}" \
        "$(interface_value "FW_INTERFACE${iface}_IP")" > "${RECEIVED}" 2>&1

    if ! grep -Eq --ignore-case "${REGEX_REACHABLE}" "${RECEIVED}"; then
        fail "${ERROR_NO_ECHO_RESPONSE}"
        print_log
    fi
}

#
# Transmission Control Protocol (TCP) tests
#
run_tcp_test() {
    src_iface=$1
    dst_iface=$2

    # Listen for traffic on dst host
    ip netns exec "namespace${dst_iface}" nc -l "${TCP_PORT}" > "${RECEIVED}" &
    listener=$!

    # Send traffic, from the src host, to the dst host
    ip netns exec "namespace${src_iface}" \
        nc -w "${TIMEOUT}" -N "$(interface_value "INTERFACE${dst_iface}_HOST_IP")" "${TCP_PORT}" < "${SENT}"
    exit_code=$?

    if [ "${exit_code}" -ne "${EXIT_SUCCESS}" ]; then
        stop_listener_if_running "${listener}"
        fail "${ERROR_TRANSMIT_FAILED}"
        print_log
        return
    fi

    # Verify that the data was transmitted correctly
    if ! diff "${SENT}" "${RECEIVED}" > /dev/null 2>&1; then
        fail "${ERROR_DATA_INCORRECT}"
        print_log
    fi
}

#
# User Datagram Protocol (UDP) tests
#
run_udp_test() {
    src_iface=$1
    dst_iface=$2

    # Listen for traffic on the dst host
    ip netns exec "namespace${dst_iface}" nc -ul "${UDP_PORT}" > "${RECEIVED}" &
    listener=$!

    # Send traffic, from the src host, to the dst host
    ip netns exec "namespace${src_iface}" \
        nc -u -q "${TIMEOUT}" "$(interface_value "INTERFACE${dst_iface}_HOST_IP")" "${UDP_PORT}" < "${SENT}"
    exit_code=$?

    stop_listener_if_running "${listener}"

    if [ "${exit_code}" -ne "${EXIT_SUCCESS}" ]; then
        fail "${ERROR_TRANSMIT_FAILED}"
        print_log
        return
    fi

    # Verify that the data was transmitted correctly
    if ! diff "${SENT}" "${RECEIVED}" > /dev/null 2>&1; then
        fail "${ERROR_DATA_INCORRECT}"
        print_log
    fi
}

#
# Rule tests
#
run_rule_application_and_removal_test() {
    src_iface=$1
    dst_iface=$2

    # The default rule for traffic on each interface is to allow it, so we setup
    # a drop traffic rule and verify that we receive no traffic.

    # Craft a JSON request with the rule's parameters
    json=$(jq \
        --null-input \
        --argjson interface "${src_iface}" \
        --argjson action "${FIREWALL_ACTION_DROP}" \
        --arg src_ip "$(interface_value "INTERFACE${src_iface}_HOST_IP")" \
        --arg src_port "" \
        --argjson src_subnet "$(interface_value "FW_INTERFACE${src_iface}_SUBNET")" \
        --arg dest_ip "$(interface_value "INTERFACE${dst_iface}_HOST_IP")" \
        --arg dest_port "${TCP_PORT}" \
        --argjson dest_subnet "$(interface_value "FW_INTERFACE${dst_iface}_SUBNET")" \
        "${TEMPLATE_JSON}")

    # Apply the rule
    response=$(api_request POST '/api/rules/tcp' "${json}") || {
        fail "${ERROR_FAILED_TO_APPLY_RULE}"
        return
    }

    # Extract the rule's ID
    rule_id=$(printf '%s' "${response}" | jq -r '.rule.id // empty')
    if [ -z "${rule_id}" ]; then
        fail "${ERROR_FAILED_TO_APPLY_RULE}"
        print_log
        return
    fi

    # Listen for traffic on the dst host
    ip netns exec "namespace${dst_iface}" nc -l "${TCP_PORT}" > "${RECEIVED}" &
    listener=$!

    # Attempt to send traffic, from the src host, to the dst host
    ip netns exec "namespace${src_iface}" \
        nc -w "${TIMEOUT}" -N "$(interface_value "INTERFACE${dst_iface}_HOST_IP")" "${TCP_PORT}" < "${SENT}"

    stop_listener_if_running "${listener}"

    # Verify that no data was received
    if ! diff /dev/null "${RECEIVED}" > /dev/null 2>&1; then
        fail "${ERROR_DATA_WAS_NOT_DROPPED}"
        print_log
    fi

    # Remove the rule
    response=$(api_request DELETE "/api/rules/tcp/${rule_id}/${src_iface}") || {
        fail "${ERROR_FAILED_TO_REMOVE_RULE}"
        print_log
        return
    }

    # Check if the response contains an error
    if printf '%s' "${response}" | grep -q '"error"'; then
        fail "${ERROR_FAILED_TO_REMOVE_RULE}"
        print_log
        return
    fi

    # Verify that the rule was removed; in other words, data transmission should
    # now succeed

    # Listen for traffic on the dst host
    ip netns exec "namespace${dst_iface}" nc -l "${TCP_PORT}" > "${RECEIVED}" &
    listener=$!

    # Send traffic, from the src host, to the dst host
    ip netns exec "namespace${src_iface}" \
        nc -w "${TIMEOUT}" -N "$(interface_value "INTERFACE${dst_iface}_HOST_IP")" "${TCP_PORT}" < "${SENT}"

    stop_listener_if_running "${listener}"

    # Verify that the data was transmitted correctly
    if ! diff "${SENT}" "${RECEIVED}" > /dev/null 2>&1; then
        fail "${ERROR_RULE_STILL_APPLIED}"
        print_log
    fi
}

interface_value() {
    local var_name=$1
    printf '%s' "${!var_name}"
}

all_interfaces() {
    iface=0
    while [ "${iface}" -lt "${INTERFACE_COUNT}" ]; do
        printf '%s\n' "${iface}"
        iface=$((iface + 1))
    done
}

api_base_url() {
    printf 'http://%s' "$(interface_value "FW_INTERFACE${WEBSERVER_INTERFACE}_IP")"
}

api_request() {
    method=$1
    path=$2

    shift 2

    if [ "$#" -gt 0 ]; then
        curl --silent --show-error \
            --header 'Content-Type: application/json' \
            --request "${method}" \
            --data "$1" \
            "$(api_base_url)${path}"
    else
        curl --silent --show-error \
            --header 'Content-Type: application/json' \
            --request "${method}" \
            "$(api_base_url)${path}"
    fi
}

stop_listener_if_running() {
    listener_pid=$1

    if kill -0 "${listener_pid}" > /dev/null 2>&1; then
        kill "${listener_pid}" > /dev/null 2>&1
    fi
}

define_generated_tests() {
    for iface in $(all_interfaces); do
        eval "
test_icmp_ping_firewall_from_interface${iface}_network() {
    run_firewall_ping_test ${iface}
}
"
    done

    for src_iface in $(all_interfaces); do
        for dst_iface in $(all_interfaces); do
            if [ "${src_iface}" -eq "${dst_iface}" ]; then
                continue
            fi

            eval "
test_icmp_ping_host_interface${src_iface}_to_interface${dst_iface}() {
    run_ping_host_test ${src_iface} ${dst_iface}
}

test_icmp_ping_unreachable_host_interface${src_iface}_to_interface${dst_iface}() {
    run_unreachable_host_test ${src_iface} ${dst_iface}
}

test_icmp_ping_unreachable_net_interface${src_iface}_to_interface${dst_iface}() {
    run_unreachable_net_test ${src_iface} ${dst_iface}
}

test_tcp_interface${src_iface}_to_interface${dst_iface}() {
    run_tcp_test ${src_iface} ${dst_iface}
}

test_udp_interface${src_iface}_to_interface${dst_iface}() {
    run_udp_test ${src_iface} ${dst_iface}
}

test_rule_application_and_removal_interface${src_iface}_to_interface${dst_iface}() {
    run_rule_application_and_removal_test ${src_iface} ${dst_iface}
}
"
        done
    done
}

define_generated_tests

suite() {
    local iface src_iface dst_iface

    for iface in $(all_interfaces); do
        suite_addTest "test_icmp_ping_firewall_from_interface${iface}_network"
    done

    for src_iface in $(all_interfaces); do
        for dst_iface in $(all_interfaces); do
            if [ "${src_iface}" -eq "${dst_iface}" ]; then
                continue
            fi

            suite_addTest "test_icmp_ping_host_interface${src_iface}_to_interface${dst_iface}"
            suite_addTest "test_icmp_ping_unreachable_host_interface${src_iface}_to_interface${dst_iface}"
            suite_addTest "test_icmp_ping_unreachable_net_interface${src_iface}_to_interface${dst_iface}"
            suite_addTest "test_tcp_interface${src_iface}_to_interface${dst_iface}"
            suite_addTest "test_udp_interface${src_iface}_to_interface${dst_iface}"
            suite_addTest "test_rule_application_and_removal_interface${src_iface}_to_interface${dst_iface}"
        done
    done
}

# Once shUnit2 has been sourced, it will find all functions that begin with the
# name `test` and add them to a list to be executed. The source statement should
# be the last line in the file.
. shunit2
