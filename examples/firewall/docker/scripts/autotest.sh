#!/usr/bin/env sh

# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause

# The following tests run within the Docker container and assume that the
# firewall is running and is configured per the `firewall_configuration.sh`
# script. From that script, we use the following values
# - EXT_HOST_IP
# - INT_HOST_IP
# - EXT_BAD_HOST_IP
# - INT_BAD_HOST_IP
# - FW_INT_IP
# - FW_INT_SUBNET
# - FW_EXT_IP
# - FW_EXT_SUBNET
#
# Additionally, the tests expect that allow rules exist for traffic on
# `UDP_PORT` and `TCP_PORT` for `EXT_HOST_IP` and `INT_HOST_IP`.
#
# The shUnit2 framework is used for setup, teardown and temporary file handling.
# For further information on shUnit2, see its documentation available at:
# https://github.com/kward/shunit2

#
# Constants
#

EXIT_SUCCESS=0

ERROR_NO_ECHO_RESPONSE='Did not receive echo response'
ERROR_UNREACHABLE='Did not receive destination host unreachable'
ERROR_TRANSMIT_FAILED='Failed to transmit data'
ERROR_DATA_INCORRECT='The received data is different to what was sent'
ERROR_DATA_WAS_NOT_DROPPED='Firewall traffic was not dropped'
ERROR_FAILED_TO_APPLY_RULE='Failed to apply firewall rule'
ERROR_FAILED_TO_REMOVE_RULE='Failed to remove firewall rule'
INFO_SKIPPING_TEST='skipping (feature not implemented yet)'

FONT_HEADER=$(printf '\033[1m\033[36m')
FONT_RED=$(printf '\033[31m')
FONT_RESET=$(printf '\033[0m')

REGEX_REACHABLE='[1-9][0-9]* received'
REGEX_UNREACHABLE='Destination host unreachable'

TEMPLATE_SRC='$src_ip, $src_port, $src_subnet'
TEMPLATE_DEST='$dest_ip, $dest_port, $dest_subnet'
TEMPLATE_ACTION='$interface, $action'
TEMPLATE_JSON="{ ${TEMPLATE_SRC}, ${TEMPLATE_DEST}, ${TEMPLATE_ACTION} }"

FIREWALL_EXTERNAL_INTERFACE=0
FIREWALL_ACTION_DROP=2

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

    if [ "$USE_RANDOM_DATA" = true ]; then
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

        print_header 'External network interfaces'
        ip netns exec ext ifconfig

        print_header 'Internal network interfaces'
        ip netns exec int ifconfig
    fi

    if [ "${SHOW_ROUTES}" = true ]; then
        print_header 'Container routes'
        ip route

        print_header 'External network routes'
        ip netns exec ext ip route

        print_header 'Internal network routes'
        ip netns exec int ip route
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

        if [ "$CLOBBER_SESSION_LOG" = true ]; then
            cp /dev/null "${SESSION_LOG}"
        fi
    else
        print_warning 'Session logs will not be generated'
        SAVE_SESSION_LOG_ON_EXIT=false
    fi

    # If `TEST_DEBUG` is set to true, the commands executed during a test will
    # be displayed on the console.
    TEST_DEBUG=false

    print_header 'Running firewall tests...'
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

    print_header "$1"
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

test_icmp_ping_host_internal_to_external() {
    ip netns exec int \
    ping -c "${COUNT}" -w "${TIMEOUT}" "${EXT_HOST_IP}" > "${RECEIVED}" 2>&1

    if ! grep -Eq --ignore-case "${REGEX_REACHABLE}" "${RECEIVED}"; then
        fail "${ERROR_NO_ECHO_RESPONSE}"
        print_log
    fi
}

test_icmp_ping_host_external_to_internal() {
    ip netns exec ext \
    ping -c "${COUNT}" -w "${TIMEOUT}" "${INT_HOST_IP}" > "${RECEIVED}" 2>&1

    if ! grep -Eq --ignore-case "${REGEX_REACHABLE}" "${RECEIVED}"; then
        fail "${ERROR_NO_ECHO_RESPONSE}"
        print_log
    fi
}

test_icmp_ping_unreachable_host_internal_to_external() {
    print_info "this may take upto ${LONG_TIMEOUT} seconds..."

    ip netns exec int \
    ping -c "${COUNT}" -w "${LONG_TIMEOUT}" "${EXT_BAD_HOST_IP}" \
    > "${RECEIVED}" 2>&1

    if ! grep -Eq --ignore-case "${REGEX_UNREACHABLE}" "${RECEIVED}"; then
        fail "${ERROR_UNREACHABLE}"
        print_log
    fi
}

test_icmp_ping_unreachable_host_external_to_internal() {
    print_info "this may take upto ${LONG_TIMEOUT} seconds..."

    ip netns exec ext \
    ping -c "${COUNT}" -w "${LONG_TIMEOUT}" "${INT_BAD_HOST_IP}" \
    > "${RECEIVED}" 2>&1

    if ! grep -Eq --ignore-case "${REGEX_UNREACHABLE}" "${RECEIVED}"; then
        fail "${ERROR_UNREACHABLE}"
        print_log
    fi
}

test_icmp_ping_firewall_from_internal_network() {
    # Prevent test from running, but correctly report skip count
    print_info "${INFO_SKIPPING_TEST}"
    startSkipping
    assertEquals 1 1

    # ip netns exec int \
    # ping -c "${COUNT}" -w "${TIMEOUT}" "${FW_INT_IP}" > "${RECEIVED}" 2>&1

    # if ! grep -Eq --ignore-case "${REGEX_REACHABLE}" "${RECEIVED}"; then
    #     fail "${ERROR_NO_ECHO_RESPONSE}"
    #     print_log
    # fi
}

test_icmp_ping_firewall_from_external_network() {
    print_info "${INFO_SKIPPING_TEST}"
    startSkipping
    assertEquals 1 1

    # ip netns exec ext \
    # ping -c "${COUNT}" -w "${TIMEOUT}" "${FW_EXT_IP}" > "${RECEIVED}" 2>&1

    # if ! grep -Eq --ignore-case "${REGEX_REACHABLE}" "${RECEIVED}"; then
    #     fail "${ERROR_NO_ECHO_RESPONSE}"
    #     print_log
    # fi
}

#
# Transmission Control Protocol (TCP) tests
#

test_tcp_internal_to_external() {
    # Listen for traffic on the external host
    ip netns exec ext \
    nc -l "${TCP_PORT}" > "${RECEIVED}" &
    listener=$!

    # Send traffic, from the internal host, to the external host
    ip netns exec int \
    nc -w "${TIMEOUT}" -N "${EXT_HOST_IP}" "${TCP_PORT}" < "${SENT}"
    exit_code=$?

    if [ "${exit_code}" -ne "${EXIT_SUCCESS}" ]; then
        kill "${listener}" > /dev/null 2>&1
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

test_tcp_external_to_internal() {
    # Listen for traffic on the internal host
    ip netns exec int \
    nc -l "${TCP_PORT}" > "${RECEIVED}" &
    listener=$!

    # Send traffic, from the external host, to the internal host
    ip netns exec ext \
    nc -w "${TIMEOUT}" -N "${INT_HOST_IP}" "${TCP_PORT}" < "${SENT}"
    exit_code=$?

    if [ "${exit_code}" -ne "${EXIT_SUCCESS}" ]; then
        kill "${listener}" > /dev/null 2>&1
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

test_udp_internal_to_external() {
    # Listen for traffic on the external host
    ip netns exec ext \
    nc -ul "${UDP_PORT}" > "${RECEIVED}" &
    listener=$!

    # Send traffic, from the internal host, to the external host
    ip netns exec int \
    nc -u -q "${TIMEOUT}" "${EXT_HOST_IP}" "${UDP_PORT}" < "${SENT}"
    exit_code=$?
    kill "${listener}" > /dev/null 2>&1

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

test_udp_external_to_internal() {
    # Listen for traffic on the internal host
    ip netns exec int \
    nc -ul "${UDP_PORT}" > "${RECEIVED}" &
    listener=$!

    # Send traffic, from the external host, to the internal host
    ip netns exec ext \
    nc -u -q "${TIMEOUT}" "${INT_HOST_IP}" "${UDP_PORT}" < "${SENT}"
    exit_code=$?
    kill "${listener}" > /dev/null 2>&1

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

test_rule_application_and_removal() {
    # The default rule for traffic on each interface is to allow it, so we setup
    # a drop traffic rule and verify that we receive no traffic.

    # Craft a JSON request with the rule's parameters
    json=$(jq \
        --null-input \
        --argjson interface "${FIREWALL_EXTERNAL_INTERFACE}" \
        --argjson action "${FIREWALL_ACTION_DROP}" \
        --arg src_ip "${EXT_HOST_IP}" \
        --arg src_port "" \
        --argjson src_subnet "${FW_EXT_SUBNET}" \
        --arg dest_ip "${INT_HOST_IP}" \
        --arg dest_port "${TCP_PORT}" \
        --argjson dest_subnet "${FW_INT_SUBNET}" \
        "${TEMPLATE_JSON}")

    # Apply the rule
    response=$(curl --silent \
        --header 'Content-Type: application/json' \
        --request 'POST' \
        --data "$json" "http://${FW_INT_IP}/api/rules/tcp")

    # Extract the rule's ID
    rule_id=$(echo "$response" | sed -E 's/.*"id": ([0-9]+).*/\1/')

    if [ -z "${rule_id}" ]; then
        fail "${ERROR_FAILED_TO_APPLY_RULE}"
        return
    fi

    # Listen for traffic on the internal host
    ip netns exec int \
    nc -l "${TCP_PORT}" > "${RECEIVED}" &
    listener=$!

    # Attempt to send traffic, from the external host, to the internal host
    ip netns exec ext \
    nc -w "${TIMEOUT}" -N "${INT_HOST_IP}" "${TCP_PORT}" < "${SENT}"
    kill "${listener}" > /dev/null 2>&1

    # Verify that no data was received
    if ! diff /dev/null "${RECEIVED}" > /dev/null 2>&1; then
        fail "${ERROR_DATA_WAS_NOT_DROPPED}"
        print_log
    fi

    # Remove the rule
    response=$(curl --silent \
        --output /dev/null \
        --header 'Content-Type: application/json' \
        --request 'DELETE' \
        "http://${FW_INT_IP}/api/rules/tcp/${rule_id}/external")

    # Check if the response contains an error
    error=$(echo "$response" | sed -E 's/.*("error":).*/\1/')

    if [ ! -z "${error}" ]; then
        fail "${ERROR_FAILED_TO_REMOVE_RULE}"
        print_log
    fi
}

#
# shUnit
#

# Load shUnit2
#
. shunit2
