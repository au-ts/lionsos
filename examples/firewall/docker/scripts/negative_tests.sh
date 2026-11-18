#!/usr/bin/env sh
# Negative integration tests for LionsOS firewall control plane
# Intended to run inside the examples/firewall Docker test container
# Requires: curl, jq

set -eu

BASE_URL="${BASE_URL:-http://localhost:8080}"
CURL_OPTS="-sS --fail"

err() { printf "[FAIL] %s\n" "$1"; exit 1; }
info() { printf "[INFO] %s\n" "$1"; }

# Helpers
get_route_count() {
  curl $CURL_OPTS "$BASE_URL/api/routes" | jq '.routes | length'
}

get_rule_count() {
  proto=$1
  iface=$2
  curl $CURL_OPTS "$BASE_URL/api/rules/$proto/$iface" | jq '.rules | length'
}

http_expect_error() {
  method=$1
  url=$2
  data=$3

  if [ -n "$data" ]; then
    resp=$(curl -sS -X "$method" -H "Content-Type: application/json" --data "$data" "$url" || true)
  else
    resp=$(curl -sS -X "$method" "$url" || true)
  fi

  # Expect JSON with "error" key or non-2xx status (curl --fail suppressed)
  echo "$resp" | jq -e 'has("error")' >/dev/null 2>&1 || return 1
  return 0
}

# Begin tests
info "Base URL: $BASE_URL"

# Ensure service available
if ! curl -sS "$BASE_URL/" >/dev/null; then
  err "Firewall UI not reachable at $BASE_URL. Run this inside the firewall test container or set BASE_URL."
fi

# Find a valid interface index
iface_count=$(curl $CURL_OPTS "$BASE_URL/api/interfaces" | jq '.interfaces | length')
if [ "$iface_count" -eq 0 ]; then
  err "No interfaces reported by firewall UI"
fi

iface=0
info "Using interface index $iface (of $iface_count) for tests"

# --- Test 1: Add route with invalid interface index ---
rc_before=$(get_route_count)
info "Routes before: $rc_before"

bad_route_json='{"interface": 9999, "ip": "10.0.0.0", "subnet": 24, "next_hop": "0"}'
if http_expect_error POST "$BASE_URL/api/routes" "$bad_route_json"; then
  info "Invalid interface: correctly rejected"
else
  err "Invalid interface: endpoint did not return expected error"
fi
rc_after=$(get_route_count)
[ "$rc_before" -eq "$rc_after" ] || err "Route count changed after invalid add (before=$rc_before after=$rc_after)"

# --- Test 2: Malformed route payload ---
if http_expect_error POST "$BASE_URL/api/routes" '{}'; then
  info "Malformed route payload: correctly rejected"
else
  err "Malformed route payload: endpoint did not return expected error"
fi
rc_after2=$(get_route_count)
[ "$rc_before" -eq "$rc_after2" ] || err "Route count changed after malformed add (before=$rc_before after=$rc_after2)"

# --- Test 3: Delete non-existent route ---
if http_expect_error DELETE "$BASE_URL/api/routes/99999" ''; then
  info "Delete non-existent route: correctly rejected"
else
  err "Delete non-existent route: endpoint did not return expected error"
fi
rc_after3=$(get_route_count)
[ "$rc_before" -eq "$rc_after3" ] || err "Route count changed after invalid delete (before=$rc_before after=$rc_after3)"

# --- Test 4: Invalid protocol string for rules ---
if http_expect_error POST "$BASE_URL/api/rules/INVALIDPROTO" '{"interface":0,"src_ip":"0","src_subnet":0,"dest_ip":"0","dest_subnet":0,"action":1}'; then
  info "Invalid protocol string: correctly rejected"
else
  err "Invalid protocol string: endpoint did not return expected error"
fi

# --- Test 5: Duplicate rule handling (add then duplicate) ---
proto=udp
# Create a minimal valid rule JSON for UDP
rule_json=$(jq -n --argjson iface $iface '{interface: $iface, src_ip: "0.0.0.0", src_subnet: 0, dest_ip: "0.0.0.0", dest_subnet: 0, action: 1}')

rules_before=$(get_rule_count $proto $iface)
info "Rules before (proto=$proto iface=$iface): $rules_before"

# Add rule (may or may not be allowed depending on config). Try adding and capture rule id.
add_resp=$(curl -sS -X POST -H 'Content-Type: application/json' --data "$rule_json" "$BASE_URL/api/rules/$proto" || true)
add_has_error=$(echo "$add_resp" | jq -e 'has("error")' >/dev/null 2>&1 || echo "no")

if [ "$add_has_error" = "no" ]; then
  rule_id=$(echo "$add_resp" | jq -r '.rule.id')
  if [ "$rule_id" = "null" ] || [ -z "$rule_id" ]; then
    err "Adding rule produced no id and no error: $add_resp"
  fi
  info "Added rule id=$rule_id"
  # Try to add the same rule again — expect an error (duplicate/clash)
  if http_expect_error POST "$BASE_URL/api/rules/$proto" "$rule_json"; then
    info "Duplicate rule: correctly rejected"
  else
    # Clean up then fail
    curl -sS -X DELETE "$BASE_URL/api/rules/$proto/$rule_id/$iface" >/dev/null || true
    err "Duplicate rule: endpoint did not return expected error"
  fi
  # Verify rule count increased by exactly 1
  rules_after=$(get_rule_count $proto $iface)
  expected=$((rules_before + 1))
  if [ "$rules_after" -ne "$expected" ]; then
    # Attempt cleanup
    curl -sS -X DELETE "$BASE_URL/api/rules/$proto/$rule_id/$iface" >/dev/null || true
    err "Rule count not as expected after duplicate test (before=$rules_before after=$rules_after)"
  fi
  # Cleanup created rule
  curl -sS -X DELETE "$BASE_URL/api/rules/$proto/$rule_id/$iface" >/dev/null || true
  info "Cleaned up rule id=$rule_id"
else
  # If initial add was rejected (e.g., capacity/clash), ensure it didn't change counts
  add_err=$(echo "$add_resp" | jq -r '.error // "<no-error-field>"')
  info "Initial add rejected as: $add_err — skipping duplicate test."
  rules_after=$(get_rule_count $proto $iface)
  [ "$rules_before" -eq "$rules_after" ] || err "Rule count changed when initial add rejected"
fi

info "All negative tests passed"
exit 0
