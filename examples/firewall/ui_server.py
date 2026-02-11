# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

from microdot import Microdot, Response
import lions_firewall


############ Network Constants ############

EthHWAddrLen = 6
IPAddrLen = 4
maxIpDigit = 255
maxPortNum = 65535
maxSubnetMask = 32

############ System Constants and Errors ############

OSErrOkay = 0
OSErrInvalidInterface = 1
OSErrInvalidProtocol = 2
OSErrInvalidRouteID = 3
OSErrInvalidRuleID = 4
OSErrInvalidRouteArgs = 5
OSErrDuplicate = 6
OSErrClash = 7
OSErrInvalidArguments = 8
OSErrInvalidRouteNum = 9
OSErrInvalidRuleNum = 10
OSErrOutOfMemory = 11
OSErrInternalError = 12
OSErrInvalidInput = 13

OSErrStrings = [
    "Ok.",
    "Invalid interface ID supplied.",
    "No matching filter for supplied protocol number.",
    "No route matching supplied route ID.",
    "No rule matching supplied rule ID, or rule ID is for default action.",
    "Invalid arguments supplied to add route.",
    "Route or rule supplied already exists.",
    "Route or rule supplied clashes with an existing route or rule.",
    "Too many or too few arguments supplied.",
    "Route number supplied is greater than the number of routes.",
    "Rule number supplied is the default action rule index, or greater than the number of rules.",
    "Internal data structures are already at capacity.",
    "Unknown internal error.",
    "Input supplied does not match the format of the field."
]

UnknownErrStr = "Unexpected unknown error."

numInterfaces = 2

interfaceStrings = [
    "external",
    "internal"
]

interfaceStringsCap = [
    "External",
    "Internal"
]

protocolNums = {
    "icmp": 0x01,
    "tcp": 0x06,
    "udp": 0x11
}

actionNums = {
    1: "Allow",
    2: "Drop",
    3: "Connect"
}

defaultActionRuleIdx = 0

############ Helper Functions ############

def htons(portNum):
  if portNum < 0 or portNum > maxPortNum:
    print(f"UI SERVER|ERR: Supplied port number {portNum} is negative or too large.")
    raise OSError(OSErrInvalidInput, OSErrStrings[OSErrInvalidInput])
  return ((portNum & 0xFF) << 8) | ((portNum & 0xFF00) >> 8)

def ipToInt(ipString):
    ipSplit = ipString.split(".")
    if not len(ipSplit) == 4:
        print(f"UI SERVER|ERR: Incorrect format of supplied IP {ipString}.")
        raise OSError(OSErrInvalidInput, OSErrStrings[OSErrInvalidInput])

    ipList = []
    for strDigit in ipSplit:
        try:
            digit = int(strDigit)
            ipList.append(digit)
        except:
            print(f"UI SERVER|ERR: Supplied IP digit {strDigit} is not a valid integer.")
            raise OSError(OSErrInvalidInput, OSErrStrings[OSErrInvalidInput])

    for digit in ipList:
        if digit < 0 or digit > maxIpDigit:
            print(f"UI SERVER|ERR: Supplied IP digit {digit} is negative or too large.")
            raise OSError(OSErrInvalidInput, OSErrStrings[OSErrInvalidInput])

    ipInt = 0
    for i in range(4):
        ipInt += (ipList[i] << (8 * i))
    return ipInt

def intToIp(ipInt):
    ipString = ""
    prevMaskSum = 0
    for i in range(4):
        mask = pow(2, 8 * (1 + i)) - 1 - prevMaskSum
        if i:
            ipString = ipString + "."
        ipString = ipString + str((ipInt & mask) >> (8 * i))
        prevMaskSum += mask
    return ipString

def tupleToMac(macList):
    macList = list(macList)
    if len(macList) != EthHWAddrLen:
        print(f"UI SERVER|ERR: System supplied MAC address {macList} has too many digits.")
        raise OSError(OSErrInternalError, OSErrStrings[OSErrInternalError])

    # Switch big to little endian
    hexList = list(map(lambda digit: hex(digit)[2:], macList))

    # Ensure digits are in the right format
    for i in range(len(hexList)):
        if len(hexList[i]) > 2:
            print(f"UI SERVER|ERR: System supplied MAC address {macList} contains a digit that is too large.")
            raise OSError(OSErrInternalError, OSErrStrings[OSErrInternalError])
        elif len(hexList[i]) < 2:
            hexList[i] = "0" + hexList[i]

    mac = ":".join(hexList)
    return mac

def interfaceStringToInt(componentType, interfaceStr):
  if componentType == "router":
      for i in range(numInterfaces):
        if interfaceStr == interfaceStrings[1-i]:
            return i
  elif componentType == "filter":
    for i in range(numInterfaces):
        if interfaceStr == interfaceStrings[i]:
            return i

    print(f"UI SERVER|ERR: Supplied interface string {interfaceStr} does not match existing interfaces.")
    raise OSError(OSErrInvalidInterface, OSErrStrings[OSErrInvalidInterface])


############ Route APIs ############

app = Microdot()

###### Interface methods ######

# Get the number of interfaces
@app.route('/api/interfaces/count', methods=['GET'])
def interfaceCount(request):
    return {"count": numInterfaces}


# Get interface details
@app.route('/api/interfaces/<int:interfaceInt>', methods=['GET'])
def interfaceDetails(request, interfaceInt):
    try:
        if interfaceInt < 0 or interfaceInt >= numInterfaces:
            print(f"UI SERVER|ERR: Supplied interface integer {interfaceInt} does not match existing interfaces.")
            raise OSError(OSErrInvalidInput, OSErrStrings[OSErrInvalidInput])

        return {
                "interface": interfaceStringsCap[interfaceInt],
                "mac": tupleToMac(lions_firewall.interface_mac_get(interfaceInt)),
                "ip": intToIp(lions_firewall.interface_ip_get(interfaceInt)),
            }
    except OSError as OSErr:
        print(f"UI SERVER|ERR: OS Error: interfaceDetails: {OSErrStrings[OSErr.errno]}")
        return {"error": OSErrStrings[OSErr.errno]}, 404
    except Exception as exception:
        print(f"UI SERVER|ERR: Unknown Error: interfaceDetails: {exception}.")
        return {"error": UnknownErrStr}, 404


###### Routing config methods ######

# Get routes for an interface
@app.route('/api/routes/<string:interfaceStr>', methods=['GET'])
def getRoutes(request, interfaceStr):
    try:
        interface = interfaceStringToInt("router", interfaceStr)
        routes = []
        route_count = lions_firewall.route_count(interface)
        for i in range(route_count):
            route = lions_firewall.route_get_nth(interface, i)
            routes.append({
                "id": route[0],
                "ip": intToIp(route[1]),
                "subnet": route[2],
                "next_hop": intToIp(route[3])
            })
        return {"routes": routes}
    except OSError as OSErr:
        print(f"UI SERVER|ERR: OS Error: getRoutes: {OSErrStrings[OSErr.errno]}")
        return {"error": OSErrStrings[OSErr.errno]}, 404
    except Exception as exception:
        print(f"UI SERVER|ERR: Unknown Error: getRoutes: {exception}.")
        return {"error": UnknownErrStr}, 404

# Delete a route from an interface
@app.route('/api/routes/<int:routeId>/<string:interfaceStr>', methods=['DELETE'])
def deleteRoute(request, routeId, interfaceStr):
    try:
        interface = interfaceStringToInt("router", interfaceStr)
        lions_firewall.route_delete(interface, routeId)
        return {"status": "ok"}
    except OSError as OSErr:
        print(f"UI SERVER|ERR: OS Error: deleteRoute: {OSErrStrings[OSErr.errno]}")
        return {"error": OSErrStrings[OSErr.errno]}, 404
    except Exception as exception:
        print(f"UI SERVER|ERR: Unknown Error: deleteRoute: {exception}.")
        return {"error": UnknownErrStr}, 404


# Add a route to an interface
@app.route('/api/routes', methods=['POST'])
def addRoute(request):
    try:
        newRoute = request.json
        interfaceInt = newRoute.get("interface")
        if interfaceInt < 0 or interfaceInt >= numInterfaces:
            print(f"UI SERVER|ERR: Supplied interface integer {interfaceInt} does not match existing interfaces.")
            raise OSError(OSErrInvalidInput, OSErrStrings[OSErrInvalidInput])

        subnet = newRoute.get("subnet")
        if subnet < 0 or subnet > maxSubnetMask:
            print(f"UI SERVER|ERR: Supplied subnet mask {subnet} is invalid.")
            raise OSError(OSErrInvalidInput, OSErrStrings[OSErrInvalidInput])

        # No IP needed for subnet == 0: route matches all IP
        if subnet == 0:
            ip = 0
        else:
            ip = ipToInt(newRoute.get("ip"))

        nextHop = newRoute.get("next_hop")
        if len(nextHop) == 0 or nextHop == "0":
          nextHop = 0
        else:
          nextHop = ipToInt(nextHop)

        lions_firewall.route_add(interfaceInt, ip, subnet, nextHop)
        newRouteOut = {"interface": interfaceInt, "ip": ip, "next_hop": nextHop}

        return {"status": "ok", "route": newRouteOut}, 201
    except OSError as OSErr:
        print(f"UI SERVER|ERR: OS Error: addRoute: {OSErrStrings[OSErr.errno]}")
        return {"error": OSErrStrings[OSErr.errno]}, 404
    except Exception as exception:
        print(f"UI SERVER|ERR: Unknown Error: addRoute: {exception}.")
        return {"error": UnknownErrStr}, 404


###### Filter rule methods ######

# Get rules and default rules for an interface filter
@app.route('/api/rules/<string:protocolStr>/<string:interfaceStr>', methods=['GET'])
def getRules(request, protocolStr, interfaceStr):
    try:
        interface = interfaceStringToInt("filter", interfaceStr)

        if protocolStr not in protocolNums.keys():
            print(f"UI SERVER|ERR: Supplied protocol string {protocolStr} does not match existing filters.")
            raise OSError(OSErrInvalidInput, OSErrStrings[OSErrInvalidInput])
        protocol = protocolNums[protocolStr]

        defaultAction = lions_firewall.filter_get_default_action(interface, protocol)
        rules = []
        # ignore default rule at position 0
        for i in range(defaultActionRuleIdx + 1,lions_firewall.rule_count(interface, protocol)):
            rule = lions_firewall.rule_get_nth(interface, protocol, i)
            rules.append({
                "id": rule[0],
                "src_ip": intToIp(rule[1]),
                "src_port": htons(rule[2]),
                "src_port_any": rule[3],
                "dest_ip": intToIp(rule[4]),
                "dest_port": htons(rule[5]),
                "dest_port_any": rule[6],
                "src_subnet": rule[7],
                "dest_subnet": rule[8],
                "action": actionNums[rule[9]]
            })
        return {"default_action": defaultAction, "rules": rules}
    except OSError as OSErr:
        print(f"UI SERVER|ERR: OS Error: getRules: {OSErrStrings[OSErr.errno]}")
        return {"error": OSErrStrings[OSErr.errno]}, 404
    except Exception as exception:
        print(f"UI SERVER|ERR: Unknown Error: getRules: {exception}.")
        return {"error": UnknownErrStr}, 404


# Delete a rule for an interface filter
@app.route('/api/rules/<string:protocolStr>/<int:ruleId>/<string:interfaceStr>', methods=['DELETE'])
def deleteRule(request, protocolStr, ruleId, interfaceStr):
    try:
        interface = interfaceStringToInt("filter", interfaceStr)

        if protocolStr not in protocolNums.keys():
            print(f"UI SERVER|ERR: Supplied protocol string {protocolStr} does not match existing filters.")
            raise OSError(OSErrInvalidInput, OSErrStrings[OSErrInvalidInput])
        protocol = protocolNums[protocolStr]

        lions_firewall.rule_delete(interface, ruleId, protocol)
        return {"status": "ok"}
    except OSError as OSErr:
        print(f"UI SERVER|ERR: OS Error: deleteRule: {OSErrStrings[OSErr.errno]}")
        return {"error": OSErrStrings[OSErr.errno]}, 404
    except Exception as exception:
        print(f"UI SERVER|ERR: Unknown Error: deleteRule: {exception}.")
        return {"error": UnknownErrStr}, 404


# Add a new default action for an interface filter
@app.route('/api/rules/<string:protocolStr>/default/<int:action>/<string:interfaceStr>', methods=['POST'])
def setDefaultAction(request, protocolStr, action, interfaceStr):
    try:
        interface = interfaceStringToInt("filter", interfaceStr)

        if protocolStr not in protocolNums.keys():
            print(f"UI SERVER|ERR: Supplied protocol string {protocolStr} does not match existing filters.")
            raise OSError(OSErrInvalidInput, OSErrStrings[OSErrInvalidInput])
        protocol = protocolNums[protocolStr]

        lions_firewall.filter_set_default_action(interface, protocol, action)
        return {"status": "ok"}, 201
    except OSError as OSErr:
        print(f"UI SERVER|ERR: OS Error: setDefaultAction: {OSErrStrings[OSErr.errno]}")
        return {"error": OSErrStrings[OSErr.errno]}, 404
    except Exception as exception:
        print(f"UI SERVER|ERR: Unknown Error: setDefaultAction: {exception}.")
        return {"error": UnknownErrStr}, 404


# Add a new rule for an interface filter
@app.route('/api/rules/<string:protocolStr>', methods=['POST'])
def addRule(request, protocolStr):
    try:
        if protocolStr not in protocolNums.keys():
            print(f"UI SERVER|ERR: Supplied protocol string {protocolStr} does not match existing filters.")
            raise OSError(OSErrInvalidInput, OSErrStrings[OSErrInvalidInput])
        protocol = protocolNums[protocolStr]

        newRule = request.json
        interfaceInt = newRule.get("interface")
        if interfaceInt < 0 or interfaceInt >= numInterfaces:
            print(f"UI SERVER|ERR: Supplied interface integer {interfaceInt} does not match existing interfaces.")
            raise OSError(OSErrInvalidInput, OSErrStrings[OSErrInvalidInput])

        srcSubnet = newRule.get("src_subnet")
        if srcSubnet < 0 or srcSubnet > maxSubnetMask:
            print(f"UI SERVER|ERR: Supplied source subnet mask {srcSubnet} is invalid.")
            raise OSError(OSErrInvalidInput, OSErrStrings[OSErrInvalidInput])

        # No IP needed for subnet == 0: rule matches all IP
        if srcSubnet == 0:
            srcIp = 0
        else:
            srcIp = ipToInt(newRule.get("src_ip"))

        destSubnet = newRule.get("dest_subnet")
        if destSubnet < 0 or destSubnet > maxSubnetMask:
            print(f"UI SERVER|ERR: Supplied destination subnet mask {destSubnet} is invalid.")
            raise OSError(OSErrInvalidInput, OSErrStrings[OSErrInvalidInput])

        # No IP needed for subnet == 0: rule matches all IP
        if destSubnet == 0:
            destIp = 0
        else:
            destIp = ipToInt(newRule.get("dest_ip"))

        action = newRule.get("action")
        if action not in actionNums.keys():
            print(f"UI SERVER|ERR: Supplied invalid action {action}.")
            raise OSError(OSErrInvalidInput, OSErrStrings[OSErrInvalidInput])

        srcPort = newRule.get("src_port")
        if not srcPort or protocol == protocolNums["icmp"]:
            srcPort = 0
            srcPortAny = True
        else:
            srcPort = htons(int(srcPort))
            srcPortAny = False

        destPort = newRule.get("dest_port")
        if not destPort or protocol == protocolNums["icmp"]:
            destPort = 0
            destPortAny = True
        else:
            destPort = htons(int(destPort))
            destPortAny = False

        ruleId = lions_firewall.rule_add(interfaceInt, protocol, srcIp, srcPort, srcPortAny,
                                         srcSubnet, destIp, destPort, destPortAny, destSubnet, action)
        return {"status": "ok", "rule": {"id": ruleId}}, 201
    except OSError as OSErr:
        print(f"UI SERVER|ERR: OS Error: addRule: {OSErrStrings[OSErr.errno]}")
        return {"error": OSErrStrings[OSErr.errno]}, 404
    except Exception as exception:
        print(f"UI SERVER|ERR: Unknown Error: addRule: {exception}.")
        return {"error": UnknownErrStr}, 404

###### Ping Response methods ######

# Set ping response for an interface
@app.route('/api/ping/<string:interfaceStr>/<int:enabled>', methods=['POST'])
def setPingResponse(request, interfaceStr, enabled):
    try:
        interface = interfaceStringToInt("filter", interfaceStr)
        lions_firewall.ping_response_set(interface, bool(enabled))
        return {
            "interface": interfaceStringsCap[interface],
            "ping_enabled": bool(enabled)
        }
    except OSError as OSErr:
        print(f"UI SERVER|ERR: OS Error: setPingResponse: {OSErrStrings[OSErr.errno]}")
        return {"error": OSErrStrings[OSErr.errno]}, 404
    except Exception as exception:
        print(f"UI SERVER|ERR: Unknown Error: setPingResponse: {exception}.")
        return {"error": UnknownErrStr}, 404

############ Web UI routes ############

@app.route('/')
def index(request):
    html = """
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8">
    <title>Firewall Config</title>
    <link rel="stylesheet" href="/main.css">
  </head>
  <body>
    <h1>Firewall Configuration</h1>
    <nav>
      <a href="/">Home</a> | <a href="/routing_config">Routing Config</a> | <a href="/rules">Rules</a> | <a href="/interface">Interface</a> | <a href="/ping_settings">Ping Settings</a>
    </nav>
  </body>
</html>
"""
    return Response(body=html, headers={'Content-Type': 'text/html'})

@app.route('/interface')
def index(request):
    html = """
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8">
    <title>Firewall Config</title>
    <link rel="stylesheet" href="/main.css">
  </head>
  <body>
    <h1>Firewall Configuration</h1>
    <nav>
      <a href="/">Home</a> | <a href="/routing_config">Routing Config</a> | <a href="/rules">Rules</a> | <a href="/interface">Interface</a> | <a href="/ping_settings">Ping Settings</a>
    </nav>
    <div id="interfaces-container">
      <table border="1">
        <thead>
          <tr>
            <th>Interface</th>
            <th>MAC Address</th>
            <th>Network IP</th>
          </tr>
        </thead>
        <tbody id="interfaces-body">
          <tr><td colspan="3">Loading interface data...</td></tr>
        </tbody>
      </table>
    </div>
    <script>
      document.addEventListener("DOMContentLoaded", function() {
        var tbody = document.getElementById('interfaces-body');
        tbody.innerHTML = "";
        fetch('/api/interfaces/count')
          .then(response => response.json())
          .then(data => {
            for (let i = 0; i < data.count; i++) {
              fetch('/api/interfaces/' + i)
                .then(response => response.json())
                .then(info => {
                  let row = document.createElement('tr');
                  row.innerHTML = "<td>" + info.interface + "</td>" +
                                  "<td>" + info.mac + "</td>" +
                                  "<td>" + info.ip + "</td>";
                  tbody.appendChild(row);
                })
                .catch(err => {
                  alert("Error" + info.error);
                  let row = document.createElement('tr');
                  row.innerHTML = "<td colspan='3'>Error retrieving interface " + i + "</td>";
                  tbody.appendChild(row);
                });
            }
          })
          .catch(err => {
            tbody.innerHTML = "<tr><td colspan='3'>Error retrieving interface count</td></tr>";
          });
      });
    </script>
  </body>
</html>
"""
    return Response(body=html, headers={'Content-Type': 'text/html'})

@app.route('/routing_config')
def config(request):
    html = """
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8">
    <title>Routing Config Page</title>
    <link rel="stylesheet" href="/main.css">
  </head>
  <body>
    <h1>Routing Configuration Page</h1>
    <nav>
      <a href="/">Home</a> | <a href="/routing_config">Routing Config</a> | <a href="/rules">Rules</a> | <a href="/interface">Interface</a> | <a href="/ping_settings">Ping Settings</a>
    </nav>

    <h2>Internal Interface Routing Table</h2>
    <table border="1">
      <thead>
        <tr>
          <th>ID</th>
          <th>IP</th>
          <th>Subnet</th>
          <th>Next Hop</th>
        </tr>
      </thead>
      <tbody id="internal-routes-body">
        <tr>
          <td colspan="5">Loading routes...</td>
        </tr>
      </tbody>
    </table>

    <h2>External Interface Routing Table</h2>
    <table border="1">
      <thead>
        <tr>
          <th>ID</th>
          <th>IP</th>
          <th>Subnet</th>
          <th>Next Hop</th>
        </tr>
      </thead>
      <tbody id="external-routes-body">
        <tr>
          <td colspan="5">Loading routes...</td>
        </tr>
      </tbody>
    </table>


    <h3>Add New Route</h3>
    <p>
      Interface: <input type="radio" name="new-interface" id="new-interface-external">External<input type="radio" name="new-interface" id="new-interface-internal">Internal<br>
      IP: <input type="text" id="new-ip" placeholder="e.g. 10.0.0.0"><br>
      Subnet: <input type="number" id="new-subnet" placeholder="e.g. 24"><br>
      Next hop: <input type="text" id="new-next-hop" placeholder="e.g. 10.0.0.0"><br>
      <button id="add-route-btn">Add Route</button>
    </p>

    <script>
      document.addEventListener("DOMContentLoaded", function() {
        function loadRoutes(type) {
          var routesBody = document.getElementById(`${type}-routes-body`);
          routesBody.innerHTML = "";
          fetch(`/api/routes/${type}`)
            .then(function(response) { return response.json(); })
            .then(function(data) {
              if (data.routes.length === 0) {
                let row = document.createElement('tr');
                row.innerHTML = "<td colspan='5'>No routes available</td>";
                routesBody.appendChild(row);
              } else {
                data.routes.forEach(function(route) {
                  let row = document.createElement('tr');

                  let cellId = document.createElement('td');
                  cellId.textContent = route.id;
                  row.appendChild(cellId);

                  let cellDest = document.createElement('td');
                  cellDest.textContent = route.subnet ? route.ip : "-";
                  row.appendChild(cellDest);

                  let cellSubnet = document.createElement('td');
                  cellSubnet.textContent = route.subnet ? route.subnet : "-";
                  row.appendChild(cellSubnet);

                  let cellNextHop = document.createElement('td');
                  cellNextHop.textContent = route.next_hop;
                  row.appendChild(cellNextHop);

                  let cellActions = document.createElement('td');
                  let delBtn = document.createElement('button');
                  delBtn.textContent = "Delete";
                  delBtn.addEventListener("click", function() {
                    fetch(`/api/routes/${route.id}/${type}`, { method: 'DELETE' })
                      .then(function(response) {
                        if (!response.ok) throw new Error("Delete failed");
                        return response.json();
                      })
                      .then(function(result) {
                        alert("Route " + route.id + " deleted.");
                        loadRoutes("external");
                      })
                      .catch(function(error) {
                        alert("Error deleting route " + route.id);
                      });
                  });
                  cellActions.appendChild(delBtn);
                  row.appendChild(cellActions);

                  routesBody.appendChild(row);
                });
              }
            })
            .catch(function(err) {
              let row = document.createElement('tr');
              row.innerHTML = "<td colspan='5'>Error retrieving routes</td>";
              routesBody.appendChild(row);
            });
        }

        loadRoutes("internal");
        loadRoutes("external");

        document.getElementById('add-route-btn').addEventListener('click', function() {
          var interfaceExternal = document.getElementById('new-interface-external').checked;
          var interfaceInternal = document.getElementById('new-interface-internal').checked;
          var interface;
          if (interfaceInternal) {
            interface = 1;
          } else if (interfaceExternal) {
            interface = 0;
          } else {
            alert("Invalid interface supplied.");
            return;
          }
          var ip = document.getElementById('new-ip').value;
          var subnet = Number(document.getElementById('new-subnet').value);
          var next_hop = document.getElementById('new-next-hop').value;
          fetch(`/api/routes`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ interface: interface, ip: ip, subnet: subnet, next_hop: next_hop})
          })
          .then(function(response) {
            if (!response.ok) throw new Error('Add route failed');
            return response.json();
          })
          .then(function(result) {
            alert("Route added successfully.");
            loadRoutes("internal");
            loadRoutes("external");
          })
          .catch(function(err) {
            alert("Error adding route");
          });
        });
      });
    </script>
  </body>
</html>
"""
    return Response(body=html, headers={'Content-Type': 'text/html'})

@app.route('/rules/<string:protocol>')
def rules(request, protocol):
    html = """
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8">
    <title>Firewall Rules</title>
    <link rel="stylesheet" href="/main.css">
  </head>
  <body>
    <h1>Firewall Rules</h1>
    <nav>
      <a href="/">Home</a> | <a href="/routing_config">Routing Config</a> | <a href="/rules">Rules</a> | <a href="/interface">Interface</a> | <a href="/ping_settings">Ping Settings</a>
    </nav>
    <div style="display: flex; flex-direction: column; margin-top: 1rem">
      <a href="/rules/udp">UDP</a>
      <a href="/rules/tcp">TCP</a>
      <a href="/rules/icmp">ICMP</a>
    </div>
    <h1>INSERT_PROTOCOL_UPPER rules</h1>
    <h2>Internal Rules</h2>
    <div class="default-action-container">
      <h4>Default action</h4>
      <div>
        <select name="internal-default-action" id="internal-default-action">
          <option value="1">Allow</option>
          <option value="2">Drop</option>
          <option value="3">Connect</option>
          <option value="5">Reject</option>
        </select>
        <button id="internal-set-default-action-btn">Update Default</button>
      </div>
    </div>
    <table border="1">
      <thead>
        <tr>
          <th>ID</th>
          <th>Source IP</th>
          <th>Source Port</th>
          <th>Destination IP</th>
          <th>Destination Port</th>
          <th>Source Subnet</th>
          <th>Destination Subnet</th>
          <th>Action</th>
          <th></th>
        </tr>
      </thead>
      <tbody id="internal-rules-body">
        <tr><td colspan="5">Loading rules...</td></tr>
      </tbody>
    </table>
    <h2>External Rules</h2>
    <div class="default-action-container">
      <h4>Default action</h4>
      <div>
        <select name="external-default-action" id="external-default-action">
          <option value="">...</option>
          <option value="1">Allow</option>
          <option value="2">Drop</option>
          <option value="3">Connect</option>
          <option value="5">Reject</option>
        </select>
        <button id="external-set-default-action-btn">Update Default</button>
      </div>
    </div>
    <table border="1">
      <thead>
        <tr>
          <th>ID</th>
          <th>Source IP</th>
          <th>Source Port</th>
          <th>Destination IP</th>
          <th>Destination Port</th>
          <th>Source Subnet</th>
          <th>Destination Subnet</th>
          <th>Action</th>
          <th></th>
        </tr>
      </thead>
      <tbody id="external-rules-body">
        <tr><td colspan="5">Loading rules...</td></tr>
      </tbody>
    </table>

    <h2>Add New Rule</h2>
      Interface: <input type="radio" name="new-interface" id="new-interface-internal">Internal<input type="radio" name="new-interface" id="new-interface-external">External<br>
      Source IP: <input type="text" id="new-src-ip" placeholder="e.g. 192.168.10.3"><br>
      Source Port: <input type="number" id="new-src-port" placeholder="e.g. 24"><br>
      Source Subnet: <input type="number" id="new-src-subnet" placeholder="e.g. 16"><br>
      Destination IP: <input type="text" id="new-dest-ip" placeholder="e.g. 192.168.10.3"><br>
      Destination Port: <input type="number" id="new-dest-port" placeholder="e.g. 24"><br>
      Destination Subnet: <input type="number" id="new-dest-subnet" placeholder="e.g. 16"><br>
      Action
      <select name="action" id="new-action">
        <option value="">Rule Action</option>
        <option value="1">Allow</option>
        <option value="2">Drop</option>
        <option value="3">Connect</option>
        <option value="5">Reject</option>
      </select>
      <button id="add-rule-btn">Add Rule</button>
    </p>

    <script>
      document.addEventListener("DOMContentLoaded", function() {
        function loadRules(type) {
          var rulesBody = document.getElementById(`${type}-rules-body`);
          rulesBody.innerHTML = "";
          const defaultAction = document.getElementById(`${type}-default-action`);
          fetch(`/api/rules/INSERT_PROTOCOL/${type}`)
            .then(function(response) { return response.json(); })
            .then(function(data) {
              for (let i = 0; i < defaultAction.options.length; i++) {
                if (data.default_action == defaultAction.options[i].value) {
                  defaultAction.options[i].selected = true;
                } else {
                  defaultAction.options[i].selected = false;
                }
              }
              if (data.rules.length === 0) {
                var row = document.createElement('tr');
                row.innerHTML = "<td colspan='5'>No rules available</td>";
                rulesBody.appendChild(row);
              } else {
                data.rules.forEach(function(rule) {
                  var row = document.createElement('tr');
                  let id = row.insertCell();
                  id.textContent = rule.id;
                  let srcIp = row.insertCell();
                  srcIp.textContent = rule.src_subnet ? rule.src_ip : "-";
                  let srcPort = row.insertCell();
                  srcPort.textContent = rule.src_port_any ? "-" : rule.src_port;
                  let destIp = row.insertCell();
                  destIp.textContent = rule.dest_subnet ? rule.dest_ip : "-";
                  let destPort = row.insertCell();
                  destPort.textContent = rule.dest_port_any ? "-" : rule.dest_port;
                  let srcSubnet = row.insertCell();
                  srcSubnet.textContent = rule.src_subnet ? rule.src_subnet : "-";
                  let destSubnet = row.insertCell();
                  destSubnet.textContent = rule.dest_subnet ? rule.dest_subnet : "-";
                  let action = row.insertCell();
                  action.textContent = rule.action;
                  let buttonCell = row.insertCell();
                  let button = document.createElement("button");
                  button.textContent = "Delete";
                  button.addEventListener("click", () => {
                    deleteRule(rule.id, type);
                  });
                  buttonCell.appendChild(button);
                  console.log("This is inner html:" + row.innerHTML);
                  rulesBody.appendChild(row);
                });
              }
            })
            .catch(function(err) {
              var row = document.createElement('tr');
              row.innerHTML = "<td colspan='5'>Error retrieving rules</td>";
              rulesBody.appendChild(row);
            });
        }

        window.deleteRule = function(ruleId, type) {
          fetch(`/api/rules/INSERT_PROTOCOL/${ruleId}/${type}`, {
            method: 'DELETE',
            headers: { 'Content-Type': 'application/json' }
          })
          .then(function(response) {
            if (!response.ok) throw new Error("Delete failed");
            return response.json();
          })
          .then(function(result) {
            alert("Rule " + ruleId + " deleted.");
            loadRules("internal");
            loadRules("external");
          })
          .catch(function(error) {
            alert("Error deleting rule " + ruleId);
          });
        }

        document.getElementById(`external-set-default-action-btn`).addEventListener('click', function() {
          const newDefaultAction = document.getElementById(`external-default-action`).value;
          fetch(`/api/rules/INSERT_PROTOCOL/default/${newDefaultAction}/external`, {
            method: 'POST',
          })
          .then(function(response) {
            if (!response.ok) throw new Error("Update default action failed");
            return response.json();
          })
          .then(function(result) {
            alert("Updated default action successfully.");
            loadRules("external");
          })
          .catch(function(err) {
            alert("Error updating default action");
          });
        });

        document.getElementById(`internal-set-default-action-btn`).addEventListener('click', function() {
          const newDefaultAction = document.getElementById(`internal-default-action`).value;
          fetch(`/api/rules/INSERT_PROTOCOL/default/${newDefaultAction}/internal`, {
            method: 'POST',
          })
          .then(function(response) {
            if (!response.ok) throw new Error("Update default action failed");
            return response.json();
          })
          .then(function(result) {
            loadRules("internal");
            alert("Updated default action successfully.");
          })
          .catch(function(err) {
            alert("Error updating default action");
          });
        });

        document.getElementById('add-rule-btn').addEventListener('click', function() {
          var interfaceInternal = document.getElementById('new-interface-internal').checked;
          var interfaceExternal = document.getElementById('new-interface-external').checked;
          var interface;
          if (interfaceInternal) {
            interface = 0;
          } else if (interfaceExternal) {
            interface = 1;
          } else {
            alert("Invalid interface supplied.");
            return;
          }
          var srcIp = document.getElementById('new-src-ip').value;
          var srcPort = document.getElementById('new-src-port').value;
          var srcSubnet = Number(document.getElementById('new-src-subnet').value);
          var destIp = document.getElementById('new-dest-ip').value;
          var destPort = document.getElementById('new-dest-port').value;
          var destSubnet = Number(document.getElementById('new-dest-subnet').value);
          var action = Number(document.getElementById('new-action').value);
          const body = JSON.stringify({
            interface: interface,
            src_ip: srcIp,
            src_port: srcPort,
            src_subnet: srcSubnet,
            dest_ip: destIp,
            dest_port: destPort,
            dest_subnet: destSubnet,
            action: action,
          });
          fetch('/api/rules/INSERT_PROTOCOL', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: body,
          })
          .then(function(response) {
            if (!response.ok) throw new Error("Add rule failed");
            return response.json();
          })
          .then(function(result) {
            alert("Rule added successfully.");
            loadRules("internal");
            loadRules("external");
          })
          .catch(function(err) {
            alert("Error adding rule");
          });
        });

        loadRules("internal");
        loadRules("external");
      });
    </script>
  </body>
</html>
"""
    html = html.replace("INSERT_PROTOCOL_UPPER", protocol.upper())
    html = html.replace("INSERT_PROTOCOL", protocol)
    return Response(body=html, headers={'Content-Type': 'text/html'})

@app.route('/rules')
def rules(request):
    html = """
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8">
    <title>Firewall Rules</title>
    <link rel="stylesheet" href="/main.css">
  </head>
  <body>
    <h1>Firewall Rules</h1>
    <nav>
      <a href="/">Home</a> | <a href="/routing_config">Routing Config</a> | <a href="/rules">Rules</a> | <a href="/interface">Interface</a> | <a href="/ping_settings">Ping Settings</a>
    </nav>
    <div style="display: inline-block; margin-top: 1rem">
      <a href="/rules/udp">UDP</a>
      <a href="/rules/tcp">TCP</a>
      <a href="/rules/icmp">ICMP</a>
    </div>
  </body>
</html>
"""
    return Response(body=html, headers={'Content-Type': 'text/html'})

@app.route("/main.css")
def css(request):
    css = """
body {
  font-family: Arial;
}

.monospace {
  font-family: monospace;
}

.default-action-container {
  display: flex;
  flex-direction: column;
  margin-bottom: 1rem;
}
"""
    return Response(body=css, headers={'Content-Type': 'text/css'})
@app.route('/ping_settings')
def ping_settings(request):
    html = """
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8">
    <title>Ping Settings</title>
    <link rel="stylesheet" href="/main.css">
  </head>
  <body>
    <h1>Ping Response Settings</h1>
    <nav>
      <a href="/">Home</a> | <a href="/routing_config">Routing Config</a> | <a href="/rules">Rules</a> | <a href="/interface">Interface</a> | <a href="/ping_settings">Ping Settings</a>
  </nav>

    <h2>Toggle Ping Response111</h2>
    <p>Control whether the firewall responds to ICMP echo requests (ping) on each interface. Default disabled for all interfaces.</p>

    <div id="ping-controls-container"></div>

    <script>
      function togglePing(interfaceName, enabled) {
        fetch('/api/ping/' + interfaceName + '/' + (enabled ? 1 : 0), {
          method: 'POST'
        })
        .then(response => response.json())
        .then(data => {
          var statusSpan = document.getElementById(interfaceName + '-status');
          if (data.error) {
            statusSpan.textContent = 'Error: ' + data.error;
            statusSpan.style.color = 'red';
          } else {
            statusSpan.textContent = enabled ? 'Enabled' : 'Disabled';
            statusSpan.style.color = enabled ? 'green' : 'gray';
          }
        })
        .catch(err => {
          alert('Error toggling ping response');
        });
      }

      document.getElementById('internal-enable').addEventListener('click', () => togglePing('internal', true));
      document.getElementById('internal-disable').addEventListener('click', () => togglePing('internal', false));
      document.getElementById('external-enable').addEventListener('click', () => togglePing('external', true));
      document.getElementById('external-disable').addEventListener('click', () => togglePing('external', false));
    </script>

    <script>
      document.addEventListener("DOMContentLoaded", function() {
        const container = document.getElementById('ping-controls-container');

        // 1. Get the number of interfaces
        fetch('/api/interfaces/count')
          .then(response => response.json())
          .then(data => {
            for (let i = 0; i < data.count; i++) {

              // 2. Fetch specific details for each interface
              fetch('/api/interfaces/' + i)
                .then(response => response.json())
                .then(info => {
                  // Create the UI block
                  const interfaceDiv = document.createElement('div');
                  interfaceDiv.className = 'ping-control';

                  // Use the data from the API (info.interface and info.ip)
                  interfaceDiv.innerHTML = `
                    <h3>${info.interface} (${info.ip})</h3>
                    <button onclick="togglePing('${info.interface.toLowerCase()}', true)">Enable Ping</button>
                    <button onclick="togglePing('${info.interface.toLowerCase()}', false)">Disable Ping</button>
                    <span id="${info.interface.toLowerCase()}-status"></span>
                  `;

                  container.appendChild(interfaceDiv);
                })
                .catch(err => console.error("Error loading interface " + i, err));
            }
          });
      });
    </script>
  </body>
</html>
"""
    return Response(body=html, headers={'Content-Type': 'text/html'})

app.run(debug=True, port=80)
