#!/usr/bin/env bash

# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause

source /mnt/lionsOS/examples/firewall/docker/scripts/firewall_configuration.sh

# The following tests run within the Docker container and assume that the
# firewall is running and is configured per the `firewall_configuration.sh`
# script.

# The tests expect interface 0 and 1 to exist, and test the flow of traffic
# between these two interfaces. Additionally, the tests expect that allow rules
# exist for traffic on `UDP_PORT` and `TCP_PORT` for interfaces 0 and 1
#
# The shUnit2 framework is used for setup, teardown and temporary file handling.
# For further information on shUnit2 and its execution behaviour, please refer
# to the quickstart guide available at:
# https://github.com/kward/shunit2?tab=readme-ov-file#-quickstart

EXIT_SUCCESS=0

ERROR_NO_ECHO_RESPONSE='Did not receive echo response'
ERROR_HOST_UNREACHABLE='Did not receive destination host unreachable'
ERROR_NET_UNREACHABLE='Did not receive destination net unreachable'
ERROR_FAILED_ENABLE_PING='Could not enable ping response'
ERROR_FAILED_DISABLE_PING='Could not disable ping response'
ERROR_TRANSMIT_FAILED='Failed to transmit data'
ERROR_DATA_INCORRECT='The received data is different to what was sent'
ERROR_DATA_WAS_NOT_DROPPED='Firewall traffic was not dropped'
ERROR_FAILED_TO_APPLY_RULE='Failed to apply firewall rule'
ERROR_FAILED_TO_REMOVE_RULE='Failed to remove firewall rule'
ERROR_RULE_STILL_APPLIED='Firewall rule is still applied'
ERROR_ICMP_REJECT_NOT_APPLIED='ICMP reject rule did not block ping traffic or send a destination port unreachable message'
ERROR_UDP_REJECT_UNEXPECTED_OUTPUT='UDP reject rule did not send a destination port unreachable message'
ERROR_UNEXPECTED_ECHO_RESPONSE='Received echo response when ping responsiveness is disabled'
INFO_SKIPPING_TEST='Skipping (feature not implemented yet)'
ERROR_TIMEOUT='Did not receive time to live exceeded message'

FONT_HEADER=$(printf '\033[1m\033[36m')
FONT_RED=$(printf '\033[31m')
FONT_RESET=$(printf '\033[0m')

REGEX_REACHABLE='[1-9][0-9]* received'
REGEX_HOST_UNREACHABLE='Destination Host Unreachable'
REGEX_DESTINATION_PORT_UNREACHABLE='Destination Port Unreachable'
REGEX_NET_UNREACHABLE='Destination Net Unreachable'
REGEX_TIMEOUT='Time to live exceeded'

TEMPLATE_SRC='$src_ip, $src_port, $src_subnet'
TEMPLATE_DEST='$dest_ip, $dest_port, $dest_subnet'
TEMPLATE_INTERFACE='$interface'
TEMPLATE_ACTION='$action'
TEMPLATE_RULE_JSON="{ ${TEMPLATE_SRC}, ${TEMPLATE_DEST}, ${TEMPLATE_INTERFACE}, ${TEMPLATE_ACTION} }"

WEBSERVER_INTERFACE=1
FIREWALL_ACTION_DROP=2
FIREWALL_ACTION_REJECT=3

BROADCAST_IP_ADDR='255.255.255.255'

#
# Setup and teardown
#

# Executed once before starting tests
#
oneTimeSetUp() {
    # Test ports
    #
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
    TIMEOUT_TTL=1

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

        for ((iface=0; iface<"${FW_INTERFACE_COUNT}"; iface++)); do
            print_header "Interface${iface} network interfaces"
            ip netns exec "namespace${iface}" ifconfig
        done
    fi

    if [ "${SHOW_ROUTES}" = true ]; then
        print_header 'Container routes'
        ip route

        for ((iface=0; iface<"${FW_INTERFACE_COUNT}"; iface++)); do
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

    print_header "Running firewall tests across ${FW_INTERFACE_COUNT} interfaces..."
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

log_interface_err() {
    message=$1
    iface=$2
    dst_iface=$3

    if [ "$#" -gt 2 ]; then
        fail "${message}. Interfaces: src=${iface},dst=${dst_iface}"
    else
        fail "${message}. Interface: ${iface}"
    fi

    print_log
}

generate_test_data() {
    block_size=$1
    output=$2
    dd if=/dev/urandom bs="${block_size}" count=1 > "${output}" 2> /dev/null
}

stop_listener_if_running() {
    listener_pid=$1

    if kill -0 "${listener_pid}" > /dev/null 2>&1; then
        kill "${listener_pid}" > /dev/null 2>&1
    fi
}

api_request() {
    method=$1
    path=$2
    data=$3

    if [ "$#" -gt 2 ]; then
        curl --silent --show-error --header 'Content-Type: application/json' \
        --request ${method} \
        --data "${data}" \
        "http://${FW_IP[${WEBSERVER_INTERFACE}]}${path}"
    else
        curl --silent --show-error --header 'Content-Type: application/json' \
        --request ${method} \
        "http://${FW_IP[${WEBSERVER_INTERFACE}]}${path}"
    fi
}

#
# Internet Control Message Protocol (ICMP) tests
#
icmp_ping_host() {
    src_iface=$1
    dst_iface=$2

    ip netns exec "namespace${src_iface}" \
        ping -c "${COUNT}" -w "${TIMEOUT}" "${HOST_IP[${dst_iface}]}" > "${RECEIVED}" 2>&1

    if ! grep -Eq --ignore-case "${REGEX_REACHABLE}" "${RECEIVED}"; then
        log_interface_err "${ERROR_NO_ECHO_RESPONSE}" ${src_iface} ${dst_iface}
    fi
}

icmp_ping_unreachable_host() {
    src_iface=$1
    dst_iface=$2

    print_info "This may take up to ${LONG_TIMEOUT} seconds..."

    ip netns exec "namespace${src_iface}" \
        ping -c "${COUNT}" -w "${LONG_TIMEOUT}" "${BAD_HOST_IP[${dst_iface}]}" \
        > "${RECEIVED}" 2>&1

    if ! grep -Eq --ignore-case "${REGEX_HOST_UNREACHABLE}" "${RECEIVED}"; then
        log_interface_err "${ERROR_HOST_UNREACHABLE}" ${src_iface} ${dst_iface}
    fi
}

icmp_ping_unreachable_net() {
    src_iface=$1
    dst_iface=$2

    ip netns exec "namespace${src_iface}" \
        ping -c "${COUNT}" -w "${LONG_TIMEOUT}" "${BAD_NET_IP[${dst_iface}]}" \
        > "${RECEIVED}" 2>&1

    if ! grep -Eq --ignore-case "${REGEX_NET_UNREACHABLE}" "${RECEIVED}"; then
        log_interface_err "${ERROR_NET_UNREACHABLE}" ${src_iface} ${dst_iface}
    fi
}

icmp_ping_firewall_interface() {
    iface=$1

    original_status=$(curl --silent "http://${FW_IP[${WEBSERVER_INTERFACE}]}/api/ping/${iface}" | sed -E 's/.*enabled": (true|false).*/\1/')

    if [ "${original_status}" = "true" ]; then
        # Disable ping
        response=$(api_request 'POST' "/api/ping/${iface}/0")
        if echo "${response}" | grep -q '"error"'; then
            log_interface_err "${ERROR_FAILED_DISABLE_PING}" ${iface}
            return
        fi
    fi

    ip netns exec "namespace${iface}" \
        ping -c "${COUNT}" -w "${TIMEOUT}" "${FW_IP[${iface}]}" > "${RECEIVED}" 2>&1

    if grep -Eq --ignore-case "${REGEX_REACHABLE}" "${RECEIVED}"; then
        log_interface_err "${ERROR_UNEXPECTED_ECHO_RESPONSE}" ${iface}
        return
    fi

    # Turn ping on and check for a response.
    response=$(api_request 'POST' "/api/ping/${iface}/1")
    if echo "${response}" | grep -q '"error"'; then
        log_interface_err "${ERROR_FAILED_ENABLE_PING}" ${iface}
        return
    fi

    ip netns exec "namespace${iface}" \
        ping -c "${COUNT}" -w "${TIMEOUT}" "${FW_IP[${iface}]}" > "${RECEIVED}" 2>&1

    if ! grep -Eq --ignore-case "${REGEX_REACHABLE}" "${RECEIVED}"; then
        log_interface_err "${ERROR_NO_ECHO_RESPONSE}" ${iface}
        return
    fi

    if [ "${original_status}" = "false" ]; then
        response=$(api_request 'POST' "/api/ping/${iface}/0")
        if echo "${response}" | grep -q '"error"'; then
            log_interface_err "${ERROR_FAILED_DISABLE_PING}" ${iface}
            return
        fi
    fi
}

icmp_ping_timeout() {
    src_iface=$1
    dst_iface=$2

    ip netns exec "namespace${src_iface}" \
        ping -c "${COUNT}" -t "${TIMEOUT_TTL}" "${HOST_IP[${dst_iface}]}" \
        > "${RECEIVED}" 2>&1

    if ! grep -Eq --ignore-case "${REGEX_TIMEOUT}" "${RECEIVED}"; then
        print_file 'Ping output' "${RECEIVED}"
        log_interface_err "${ERROR_TIMEOUT}" ${src_iface} ${dst_iface}
    fi
}

icmp_reject_icmp() {
    src_iface=$1
    dst_iface=$2

    # Craft a JSON request with the rule's parameters.
    json=$(jq \
        --null-input \
        --argjson interface "${src_iface}" \
        --argjson action "${FIREWALL_ACTION_REJECT}" \
        --arg src_ip "${HOST_IP[${src_iface}]}" \
        --arg src_port "" \
        --argjson src_subnet "${FW_SUBNET[${src_iface}]}" \
        --arg dest_ip "${HOST_IP[${dst_iface}]}" \
        --arg dest_port "" \
        --argjson dest_subnet "${FW_SUBNET[${dst_iface}]}" \
        "${TEMPLATE_RULE_JSON}")

    # Apply the rule.
    response=$(api_request 'POST' "/api/rules/icmp" "${json}")

    # Extract the rule's ID.
    rule_id=$(echo "$response" | sed -E 's/.*"id": ([0-9]+).*/\1/')

    if [ -z "${rule_id}" ]; then
        log_interface_err "${ERROR_FAILED_TO_APPLY_RULE}" ${src_iface} ${dst_iface}
        return
    fi

    # Attempt ICMP echo while reject rule is active.
    ip netns exec "namespace${src_iface}" \
        ping -c "${COUNT}" -w "${TIMEOUT}" "${HOST_IP[${dst_iface}]}" > "${RECEIVED}" 2>&1

    # Ping should not succeed while the reject rule is active.
    if ! grep -Eq --ignore-case "${REGEX_DESTINATION_PORT_UNREACHABLE}" "${RECEIVED}"; then
        print_file 'Ping output' "${RECEIVED}"
        log_interface_err "${ERROR_ICMP_REJECT_NOT_APPLIED}" ${src_iface} ${dst_iface}
    fi

    # Remove the reject rule after the attempt.
    response=$(api_request 'DELETE' "/api/rules/icmp/${rule_id}/${src_iface}")
    if echo "${response}" | grep -q '"error"'; then
        log_interface_err "${ERROR_FAILED_TO_REMOVE_RULE}" ${src_iface} ${dst_iface}
    fi
}

icmp_reject_udp() {
    src_iface=$1
    dst_iface=$2

    # Craft a JSON request with the rule's parameters.
    json=$(jq \
        --null-input \
        --argjson interface "${src_iface}" \
        --argjson action "${FIREWALL_ACTION_REJECT}" \
        --arg src_ip "${HOST_IP[${src_iface}]}" \
        --arg src_port "" \
        --argjson src_subnet "${FW_SUBNET[${src_iface}]}" \
        --arg dest_ip "${HOST_IP[${dst_iface}]}" \
        --arg dest_port "" \
        --argjson dest_subnet "${FW_SUBNET[${dst_iface}]}" \
        "${TEMPLATE_RULE_JSON}")

    # Apply the rule
    response=$(api_request 'POST' "/api/rules/udp" "${json}")

    # Extract the rule's ID
    rule_id=$(echo "$response" | sed -E 's/.*"id": ([0-9]+).*/\1/')

    if [ -z "${rule_id}" ]; then
        log_interface_err "${ERROR_FAILED_TO_APPLY_RULE}" ${src_iface} ${dst_iface}
        return
    fi

    # Send UDP traffic and capture sender-side output where ICMP reject details
    # are reported.
    ip netns exec "namespace${src_iface}" \
        nc -u -z -w "${LONG_TIMEOUT}" "${HOST_IP[${dst_iface}]}" "${UDP_PORT}" \
        < "${SENT}" > /dev/null 2> "${RECEIVED}"
    exit_code=$?

    if [ "${exit_code}" -eq "${EXIT_SUCCESS}" ]; then
        log_interface_err "${ERROR_UDP_REJECT_UNEXPECTED_OUTPUT}" ${src_iface} ${dst_iface}
    fi

    # Remove the reject rule now that traffic was attempted
    response=$(api_request 'DELETE' "/api/rules/udp/${rule_id}/${src_iface}" "${json}")
    if echo "${response}" | grep -q '"error"'; then
        log_interface_err "${ERROR_FAILED_TO_REMOVE_RULE}" ${src_iface} ${dst_iface}
    fi
}

#
# Transmission Control Protocol (TCP) tests
#
tcp_connect_host() {
    src_iface=$1
    dst_iface=$2

    # Listen for traffic on dst host
    ip netns exec "namespace${dst_iface}" nc -l "${TCP_PORT}" > "${RECEIVED}" &
    listener=$!

    # Send traffic, from the src host, to the dst host
    ip netns exec "namespace${src_iface}" \
        nc -w "${TIMEOUT}" -N "${HOST_IP[${dst_iface}]}" "${TCP_PORT}" < "${SENT}"
    exit_code=$?

    stop_listener_if_running "${listener}"

    if [ "${exit_code}" -ne "${EXIT_SUCCESS}" ]; then
        log_interface_err "${ERROR_TRANSMIT_FAILED}" ${src_iface} ${dst_iface}
        return
    fi

    # Verify that the data was transmitted correctly
    if ! diff "${SENT}" "${RECEIVED}" > /dev/null 2>&1; then
        log_interface_err "${ERROR_DATA_INCORRECT}" ${src_iface} ${dst_iface}
    fi
}

#
# User Datagram Protocol (UDP) tests
#
udp_connect_host() {
    src_iface=$1
    dst_iface=$2

    # Listen for traffic on the dst host
    ip netns exec "namespace${dst_iface}" nc -ul "${UDP_PORT}" > "${RECEIVED}" &
    listener=$!

    # Send traffic, from the src host, to the dst host
    ip netns exec "namespace${src_iface}" \
        nc -u -q "${TIMEOUT}" "${HOST_IP[${dst_iface}]}" "${UDP_PORT}" < "${SENT}"
    exit_code=$?

    stop_listener_if_running "${listener}"

    if [ "${exit_code}" -ne "${EXIT_SUCCESS}" ]; then
        log_interface_err "${ERROR_TRANSMIT_FAILED}" ${src_iface} ${dst_iface}
        return
    fi

    # Verify that the data was transmitted correctly
    if ! diff "${SENT}" "${RECEIVED}" > /dev/null 2>&1; then
        log_interface_err "${ERROR_DATA_INCORRECT}" ${src_iface} ${dst_iface}
    fi
}

udp_broadcast() {
    src_iface=$1
    dst_iface=$2

    # Listen for traffic on interface 1
    ip netns exec "namespace${dst_iface}" \
        nc -ul "${UDP_PORT}" > "${RECEIVED}" &
    listener=$!

    # Send broadcast traffic from the interface 0 host
    ip netns exec "namespace${src_iface}" \
        nc -u -b -q "${TIMEOUT}" "${BROADCAST_IP_ADDR}" "${UDP_PORT}" < "${SENT}"
    exit_code=$?

    sleep "${TIMEOUT}"

    stop_listener_if_running "${listener}"

    if [ "${exit_code}" -ne "${EXIT_SUCCESS}" ]; then
        log_interface_err "${ERROR_TRANSMIT_FAILED}" ${src_iface} ${dst_iface}
        return
    fi

    if ! diff /dev/null "${RECEIVED}" > /dev/null 2>&1; then
        log_interface_err "${ERROR_DATA_WAS_NOT_DROPPED}" ${src_iface} ${dst_iface}
    fi
}

udp_subnet_broadcast() {
    src_iface=$1
    dst_iface=$2

    # Listen for traffic on interface 1
    ip netns exec "namespace${dst_iface}" \
        nc -ul "${UDP_PORT}" > "${RECEIVED}" &
    listener=$!

    # Send broadcast traffic from interface 0
    ip netns exec "namespace${src_iface}" \
        nc -u -b -q "${TIMEOUT}" "${BROADCAST_IP[${dst_iface}]}" "${UDP_PORT}" < "${SENT}"
    exit_code=$?

    sleep "${TIMEOUT}"

    stop_listener_if_running "${listener}"

    if [ "${exit_code}" -ne "${EXIT_SUCCESS}" ]; then
        log_interface_err "${ERROR_TRANSMIT_FAILED}" ${src_iface} ${dst_iface}
        return
    fi

    if ! diff /dev/null "${RECEIVED}" > /dev/null 2>&1; then
        log_interface_err "${ERROR_DATA_WAS_NOT_DROPPED}" ${src_iface} ${dst_iface}
    fi
}

#
# Rule tests
#
run_rule_application_and_removal() {
    src_iface=$1
    dst_iface=$2

    # The default rule for traffic on each interface is to allow it, so we setup
    # a drop traffic rule and verify that we receive no traffic.

    # Craft a JSON request with the rule's parameters
    json=$(jq \
        --null-input \
        --argjson interface "${src_iface}" \
        --argjson action "${FIREWALL_ACTION_DROP}" \
        --arg src_ip "${HOST_IP[${src_iface}]}" \
        --arg src_port "" \
        --argjson src_subnet "${FW_SUBNET[${src_iface}]}" \
        --arg dest_ip "${HOST_IP[${dst_iface}]}" \
        --arg dest_port "${TCP_PORT}" \
        --argjson dest_subnet "${FW_SUBNET[${dst_iface}]}" \
        "${TEMPLATE_RULE_JSON}")

    # Apply the rule
    response=$(api_request POST '/api/rules/tcp' "${json}")

    # Extract the rule's ID
    rule_id=$(printf '%s' "${response}" | jq -r '.rule.id // empty')
    if [ -z "${rule_id}" ]; then
        log_interface_err "${ERROR_FAILED_TO_APPLY_RULE}" ${src_iface} ${dst_iface}
        return
    fi

    # Listen for traffic on the dst host
    ip netns exec "namespace${dst_iface}" nc -l "${TCP_PORT}" > "${RECEIVED}" &
    listener=$!

    # Attempt to send traffic, from the src host, to the dst host
    ip netns exec "namespace${src_iface}" \
        nc -w "${TIMEOUT}" -N "${HOST_IP[${dst_iface}]}" "${TCP_PORT}" < "${SENT}"

    stop_listener_if_running "${listener}"

    # Verify that no data was received
    if ! diff /dev/null "${RECEIVED}" > /dev/null 2>&1; then
        log_interface_err "${ERROR_DATA_WAS_NOT_DROPPED}" ${src_iface} ${dst_iface}
    fi

    # Remove the rule
    response=$(api_request DELETE "/api/rules/tcp/${rule_id}/${src_iface}")
    if echo "${response}" | grep -q '"error"'; then
        log_interface_err "${ERROR_FAILED_TO_REMOVE_RULE}" ${src_iface} ${dst_iface}
        return
    fi

    # Verify that the rule was removed; in other words, data transmission should
    # now succeed

    # Listen for traffic on the dst host
    ip netns exec "namespace${dst_iface}" nc -l "${TCP_PORT}" > "${RECEIVED}" &
    listener=$!

    # Send traffic, from the src host, to the dst host
    ip netns exec "namespace${src_iface}" \
        nc -w "${TIMEOUT}" -N "${HOST_IP[${dst_iface}]}" "${TCP_PORT}" < "${SENT}"

    stop_listener_if_running "${listener}"

    # Verify that the data was transmitted correctly
    if ! diff "${SENT}" "${RECEIVED}" > /dev/null 2>&1; then
        log_interface_err "${ERROR_RULE_STILL_APPLIED}" ${src_iface} ${dst_iface}
    fi
}

#
# shUnit
#

call_all_interfaces() {
    test=$1
    for ((iface=0; iface<"${FW_INTERFACE_COUNT}"; iface++)); do
        "${test}" "${iface}"
    done
}

call_all_interface_pairs() {
    test=$1
    for ((src_iface=0; src_iface<"${FW_INTERFACE_COUNT}"; src_iface++)); do
        for ((dst_iface=0; dst_iface<"${FW_INTERFACE_COUNT}"; dst_iface++)); do
            if [ "${src_iface}" -eq "${dst_iface}" ]; then
                continue
            fi

            "${test}" "${src_iface}" "${dst_iface}"
        done
    done
}

test_icmp_ping_host() {
    call_all_interface_pairs icmp_ping_host
}

test_icmp_ping_unreachable_host() {
    call_all_interface_pairs icmp_ping_unreachable_host
}

test_icmp_ping_unreachable_net() {
    call_all_interface_pairs icmp_ping_unreachable_net
}

test_icmp_ping_firewall_interface() {
    call_all_interfaces icmp_ping_firewall_interface
}

test_icmp_icmp_ping_timeout() {
    call_all_interface_pairs icmp_ping_timeout
}

test_icmp_reject_icmp() {
    call_all_interface_pairs icmp_reject_icmp
}

test_icmp_reject_udp() {
    call_all_interface_pairs icmp_reject_udp
}

test_tcp_connect_host () {
    call_all_interface_pairs tcp_connect_host
}

test_udp_connect_host() {
    call_all_interface_pairs udp_connect_host
}

test_udp_broadcast() {
    call_all_interface_pairs udp_broadcast
}

test_udp_subnet_broadcast() {
    call_all_interface_pairs udp_subnet_broadcast
}

test_run_rule_application_and_removal() {
    call_all_interface_pairs run_rule_application_and_removal
}

# Once shUnit2 has been sourced, it will find all functions that begin with the
# name `test` and add them to a list to be executed. The source statement should
# be the last line in the file.
. shunit2
