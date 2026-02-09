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
# - FW_EXT_IP
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

FONT_HEADER=$(printf '\033[1m\033[36m')
FONT_RED=$(printf '\033[31m')
FONT_RESET=$(printf '\033[0m')

REGEX_REACHABLE='[1-9][0-9]* received'
REGEX_UNREACHABLE='Destination host unreachable'

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
    EXTERNAL_TRAFFIC="${SHUNIT_TMPDIR}/external_traffic"
    INTERNAL_TRAFFIC="${SHUNIT_TMPDIR}/internal_traffic"

    # Test data
    #
    # For tests that transfer data, random data can be generated or fixed data
    # can be supplied. Setting `USE_RANDOM_DATA` to true will generate random
    # data at a specified `SIZE_BYTES`.
    TEST_DATA='/mnt/lionsOS/examples/firewall/docker/scripts/autotest.dat'
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
    PRINT_EXTERNAL_TRAFFIC_ON_ERROR=false
    PRINT_INTERNAL_TRAFFIC_ON_ERROR=false

    if [ "${PRINT_LOG_ON_ERROR}" = true ] && [ ! -f "${LOG}" ]; then
        print_warning "Log file (${LOG}) does not exist"
        print_warning "Logs will not be output on error"
        PRINT_LOG_ON_ERROR=false
    fi

    # If `TEST_DEBUG` is set to true, the commands executed during a test will
    # be display on the console.
    TEST_DEBUG=false

    print_header 'Running firewall tests...'
}

# Executed before each test
#
setUp() {
    if [ "${PRINT_EXTERNAL_TRAFFIC_ON_ERROR}" = true ]; then
        tcpdump -ex -i tap0 > "${EXTERNAL_TRAFFIC}" 2> /dev/null &
    fi

    if [ "${PRINT_INTERNAL_TRAFFIC_ON_ERROR}" = true ]; then
        tcpdump -ex -i tap1 > "${INTERNAL_TRAFFIC}" 2> /dev/null &
    fi

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

    if [ -f "${LOG}" ]; then
        cp /dev/null "${LOG}"
    fi

    cp /dev/null "${RECEIVED}"
    cp /dev/null "${EXTERNAL_TRAFFIC}"
    cp /dev/null "${INTERNAL_TRAFFIC}"
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

print_file() {
    header=$1
    data_file=$2

    print_header "$1"
    cat "${data_file}"
    printf '\n'
}

print_traffic() {
    if [ "${PRINT_EXTERNAL_TRAFFIC_ON_ERROR}" = true ]; then
        print_file 'External traffic' "${EXTERNAL_TRAFFIC}"
    fi

    if [ "${PRINT_INTERNAL_TRAFFIC_ON_ERROR}" = true ]; then
        print_file 'Internal traffic' "${INTERNAL_TRAFFIC}"
    fi
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
    ip netns exec int \
    ping -c "${COUNT}" -w "${LONG_TIMEOUT}" "${EXT_BAD_HOST_IP}" \
    > "${RECEIVED}" 2>&1

    if ! grep -Eq --ignore-case "${REGEX_UNREACHABLE}" "${RECEIVED}"; then
        fail "${ERROR_UNREACHABLE}"
        print_log
    fi
}

test_icmp_ping_unreachable_host_external_to_internal() {
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
    printf '%s\n' 'skipping... (feature not implemented yet)'
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
    printf '%s\n' 'skipping... (feature not implemented yet)'
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
        print_traffic
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
        print_traffic
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
        print_traffic
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
        print_traffic
        print_log
    fi
}

#
# shUnit
#

# Load shUnit2
#
. shunit2
