# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

from microdot import Microdot, Response
import lions_firewall

app = Microdot()

# API routes

@app.route('/api/interfaces/<int:interface_id>', methods=['GET'])
def interface_details(request, interface_id):
    try:
        return {
            "interface": interface_id,
            "mac": lions_firewall.interface_mac_get(interface_id),
            "cidr": lions_firewall.interface_cidr_get(interface_id),
        }
    except KeyError:
        return {"error": "Interface not found"}, 404

@app.route('/api/interfaces/count', methods=['GET'])
def interface_count(request):
    return {"count": 2}

@app.route('/api/interfaces/<int:interface_id>/network', methods=['GET'])
def get_network_config(request, interface_id):
    try:
        return {"interface": interface_id, "cidr": lions_firewall.interface_cidr_get(interface_id)}
    except KeyError:
        return {"error": "Interface not found"}, 404

@app.route('/api/interfaces/<int:interface_id>/network', methods=['PUT'])
def set_network_config(request, interface_id):
    if interface_id >= 2:
        return {"error": "Interface not found"}, 404

    try:
        data = request.json
        cidr = data.get("cidr")
        if cidr is None:
            return {"error": "Missing cidr"}, 400
        lions_firewall.interface_cidr_set(interface_id, cidr)
        return {"status": "ok", "interface": interface_id, "cidr": cidr}
    except Exception as e:
        return {"error": "Invalid input"}, 400

@app.route('/api/routes/<string:interface>', methods=['GET'])
def get_routes(request, interface):
    routes = []
    for i in range(lions_firewall.route_count(interface)):
        t = lions_firewall.route_get_nth(i, interface)
        routes.append({
            "id": t[0],
            "destination": t[1],
            "subnet": t[2],
            "next_hop": t[3],
            "num_hops": t[4]
        })
    routes.sort(key=lambda route: route['id'])
    return {"routes": routes}

@app.route('/api/routes', methods=['POST'])
def add_route(request):
    try:
        print("In add route")
        data = request.json
        interface = data.get("interface")
        destination = data.get("destination")
        subnet = data.get("subnet")
        next_hop = data.get("next_hop")
        num_hops = data.get("num_hops")
        print("Finished getting values")

        if destination is None or next_hop is None:
            return {"error": "Missing destination or interface"}, 400

        print("Calling route add")
        route_id = lions_firewall.route_add(interface, destination, subnet, next_hop, num_hops)
        print("Returned from route add")

        new_route = {"id": route_id, "interface": interface, "destination": destination, "next_hop": next_hop, "num_hops": num_hops}

        return {"status": "ok", "route": new_route}, 201
    except Exception as e:
        return {"error": "Invalid input"}, 400

@app.route('/api/routes/<int:route_id>/<string:interface>', methods=['DELETE'])
def delete_route(request, route_id, interface):
    try:
        lions_firewall.route_delete(route_id, interface)
        return {"status": "ok"}
    except IndexError:
        return {"error": "Route not found"}, 404


PROTOCOLS = ["udp", "tcp", "icmp"]


@app.route('/api/rules/<string:protocol>/<string:filter>', methods=['GET'])
def get_rules(request, protocol, filter):
    if protocol not in PROTOCOLS:
        return {"error": "Invalid protocol given"}, 400

    default_action = lions_firewall.filter_get_default_action(protocol, filter)
    rules = []
    for i in range(lions_firewall.rule_count(protocol, filter)):
        t = lions_firewall.rule_get_nth(protocol, filter, i)
        rules.append({
            "id": t[0],
            "src_ip": t[1],
            "src_port": t[2],
            "dest_ip": t[3],
            "dest_port": t[4],
            "src_subnet": t[5],
            "dest_subnet": t[6],
            "src_port_any": t[7],
            "dest_port_any": t[8],
            "action": t[9],
        })
    rules.sort(key=lambda rule: rule['id'])
    return {"default_action": default_action, "rules": rules}

@app.route('/api/rules/<string:protocol>/default/<int:action>/<string:filter>', methods=['POST'])
def set_default_action(request, protocol, action, filter):
    try:
        if protocol not in PROTOCOLS:
          return {"error": "Invalid protocol given"}, 400
        print(f"This is the action we are setting to: {action}")
        lions_firewall.filter_set_default_action(protocol, filter, action)
        print(f"INFO: setting default action for protocol '{protocol}' to '{action}'")

        return {"status": "ok"}, 201
    except Exception as e:
        return {"error": "Invalid input"}, 400

@app.route('/api/rules/<string:protocol>', methods=['POST'])
def add_rule(request, protocol):
    try:
        data = request.json
        _filter = data.get("filter")
        src_ip = data.get("src_ip")
        src_port = data.get("src_port")
        src_subnet = data.get("src_subnet")
        dest_ip = data.get("dest_ip")
        dest_port = data.get("dest_port")
        dest_subnet = data.get("dest_subnet")
        action = data.get("action")

        if protocol not in PROTOCOLS:
          return {"error": "Invalid protocol given"}, 401

        if _filter != 0 and _filter != 1:
          return {"error": "Invalid filter given"}, 402
        if src_port < 0 or dest_port < 0:
          return {"error": "Invalid port given"}, 402
        if src_subnet < 0 or dest_subnet < 0:
          return {"error": "Invalid subnet given"}, 402
        if action < 1 or action > 3:
          return {"error": "Invalid action given"}, 402

        rule_id = lions_firewall.rule_add(protocol, _filter, src_ip, src_port, src_subnet,
                                          dest_ip, dest_port, dest_subnet, action)
        new_rule = {"id": rule_id}
        return {"status": "ok", "rule": new_rule}, 201
    except Exception as e:
        return {"error": "Invalid input"}, 403


@app.route('/api/rules/<string:protocol>/<int:rule_id>/<string:filter>', methods=['DELETE'])
def delete_rule(request, protocol, rule_id, filter):
    print(f"WE ARE ATTEMPTING TO DELETE THE RULE HERE?--- {filter}")
    try:
        if protocol not in PROTOCOLS:
            return {"error": "Invalid protocol given"}, 400

        lions_firewall.rule_delete(rule_id, protocol, filter)
        print("ok")
        return {"status": "ok"}
    except IndexError:
        print("404")
        return {"error": "Rule not found"}, 404


# Web UI routes

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
      <a href="/">Home</a> | <a href="/routing_config">Routing Config</a> | <a href="/rules">Rules</a> | <a href="/interface">Interface</a>
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
      <a href="/">Home</a> | <a href="/routing_config">Routing Config</a> | <a href="/rules">Rules</a> | <a href="/interface">Interface</a>
    </nav>
    <div id="interfaces-container">
      <table border="1">
        <thead>
          <tr>
            <th>Interface</th>
            <th>MAC Address</th>
            <th>Network (CIDR)</th>
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
                                  "<td>" + info.cidr + "</td>";
                  tbody.appendChild(row);
                })
                .catch(err => {
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
      <a href="/">Home</a> | <a href="/routing_config">Routing Config</a> | <a href="/rules">Rules</a> | <a href="/interface">Interface</a>
    </nav>

    <h2>External Interface Routing Table</h2>
    <table border="1">
      <thead>
        <tr>
          <th>ID</th>
          <th>Destination IP</th>
          <th>Subnet</th>
          <th>Next Hop</th>
          <th>Num Hops</th>
        </tr>
      </thead>
      <tbody id="external-routes-body">
        <tr>
          <td colspan="5">Loading routes...</td>
        </tr>
      </tbody>
    </table>

    <h2>Internal Interface Routing Table</h2>
    <table border="1">
      <thead>
        <tr>
          <th>ID</th>
          <th>Destination IP</th>
          <th>Subnet</th>
          <th>Next Hop</th>
          <th>Num Hops</th>
        </tr>
      </thead>
      <tbody id="internal-routes-body">
        <tr>
          <td colspan="5">Loading routes...</td>
        </tr>
      </tbody>
    </table>


    <h3>Add New Route</h3>
    <p>
      Interface: <input type="radio" name="new-interface" id="new-interface-internal">Internal<input type="radio" name="new-interface" id="new-interface-external">External<br>
      Destination IP: <input type="text" id="new-destination-ip" placeholder="e.g. 10.0.0.0"><br>
      Subnet: <input type="number" id="new-subnet" placeholder="e.g. 24"><br>
      Next hop: <input type="text" id="new-next-hop" placeholder="e.g. 10.0.0.0"><br>
      Num hops: <input type="number" id="new-num-hops" placeholder="e.g. 64"><br>
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
                  cellDest.textContent = route.destination;
                  row.appendChild(cellDest);

                  let cellSubnet = document.createElement('td');
                  cellSubnet.textContent = route.subnet ? route.subnet : "-";
                  row.appendChild(cellSubnet);

                  let cellNextHop = document.createElement('td');
                  cellNextHop.textContent = route.next_hop;
                  row.appendChild(cellNextHop);

                  let cellNumHops = document.createElement('td');
                  cellNumHops.textContent = route.num_hops;
                  row.appendChild(cellNumHops);

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
                        loadRoutes("internal");
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
          var interfaceInternal = document.getElementById('new-interface-internal').checked;
          var filterExternal = document.getElementById('new-interface-external').checked;
          var interface;
          if (interfaceInternal) {
            interface = 1;
          } else if (filterExternal) {
            interface = 0;
          } else {
            alert("uh oh");
            return;
          }
          var destination = document.getElementById('new-destination-ip').value;
          var subnet = Number(document.getElementById('new-subnet').value);
          var next_hop = document.getElementById('new-next-hop').value;
          var num_hops = Number(document.getElementById('new-num-hops').value);
          fetch(`/api/routes`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ interface: interface, destination: destination, subnet: subnet, next_hop: next_hop, num_hops: num_hops })
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
      <a href="/">Home</a> | <a href="/routing_config">Routing Config</a> | <a href="/rules">Rules</a> | <a href="/interface">Interface</a>
    </nav>
    <div style="display: flex; flex-direction: column; margin-top: 1rem">
      <a href="/rules/udp">UDP</a>
      <a href="/rules/tcp">TCP</a>
      <a href="/rules/icmp">ICMP</a>
    </div>
    <h1>INSERT_PROTOCOL_UPPER rules</h1>
    <h2>Existing Internal Rules</h2>
    <div class="default-action-container">
      <h4>Default action</h4>
      <div>
        <select name="internal-default-action" id="internal-default-action">
          <option value="1">Allow</option>
          <option value="2">Drop</option>
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
    <h2>Existing External Rules</h2>
    <div class="default-action-container">
      <h4>Default action</h4>
      <div>
        <select name="external-default-action" id="external-default-action">
          <option value="">...</option>
          <option value="1">Allow</option>
          <option value="2">Drop</option>
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
      Filter: <input type="radio" name="new-filter" id="new-filter-internal">Internal<input type="radio" name="new-filter" id="new-filter-external">External<br>
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
                  srcIp.textContent = rule.src_ip;
                  let srcPort = row.insertCell();
                  srcPort.textContent = rule.src_port;
                  let destIp = row.insertCell();
                  destIp.textContent = rule.dest_ip;
                  let destPort = row.insertCell();
                  destPort.textContent = rule.dest_port;
                  let srcSubnet = row.insertCell();
                  srcSubnet.textContent = rule.src_subnet;
                  let destSubnet = row.insertCell();
                  destSubnet.textContent = rule.dest_subnet;
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
            alert("Updated default action successfully.");
          })
          .catch(function(err) {
            alert("Error updating default action");
          });
        });

        document.getElementById('add-rule-btn').addEventListener('click', function() {
          var filterInternal = document.getElementById('new-filter-internal').checked;
          var filterExternal = document.getElementById('new-filter-external').checked;
          var filter;
          if (filterInternal) {
            filter = 1;
          } else if (filterExternal) {
            filter = 0;
          } else {
            alert("uh oh");
            return;
          }
          var srcIp = document.getElementById('new-src-ip').value;
          var srcPort = Number(document.getElementById('new-src-port').value);
          var srcSubnet = Number(document.getElementById('new-src-subnet').value);
          var destIp = document.getElementById('new-dest-ip').value;
          var destPort = Number(document.getElementById('new-dest-port').value);
          var destSubnet = Number(document.getElementById('new-dest-subnet').value);
          var action = Number(document.getElementById('new-action').value);
          const body = JSON.stringify({
            filter: filter,
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
      <a href="/">Home</a> | <a href="/routing_config">Routing Config</a> | <a href="/rules">Rules</a> | <a href="/interface">Interface</a>
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

app.run(debug=True, port=80)