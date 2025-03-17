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

@app.route('/api/routes', methods=['GET'])
def get_routes(request):
    routes = []
    for i in range(lions_firewall.route_count()):
        t = lions_firewall.route_get_nth(i)
        routes.append({
            "id": t[0],
            "destination": t[1],
            "gateway": t[2],
            "interface": t[3]
        })
    routes.sort(key=lambda route: route['id'])
    return {"routes": routes}

@app.route('/api/routes', methods=['POST'])
def add_route(request):
    try:
        data = request.json
        destination = data.get("destination")
        gateway = data.get("gateway")  # May be None
        iface = data.get("interface")
        if destination is None or iface is None:
            return {"error": "Missing destination or interface"}, 400
        route_id = lions_firewall.route_add(destination, gateway, iface)
        new_route = {"id": route_id, "destination": destination, "gateway": gateway, "interface": iface}
        return {"status": "ok", "route": new_route}, 201
    except Exception as e:
        return {"error": "Invalid input"}, 400

@app.route('/api/routes/<int:route_id>', methods=['DELETE'])
def delete_route(request, route_id):
    try:
        lions_firewall.route_delete(route_id)
        return {"status": "ok"}
    except IndexError:
        return {"error": "Route not found"}, 404


PROTOCOLS = ["udp", "tcp", "icmp"]


@app.route('/api/rules/<string:protocol>', methods=['GET'])
def get_rules(request, protocol):
    if protocol not in PROTOCOLS:
        return {"error": "Invalid protocol given"}, 400

    default_action = lions_firewall.filter_default_action()
    rules = []
    for i in range(lions_firewall.rule_count()):
        t = lions_firewall.rule_get_nth(i)
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

@app.route('/api/rules/<string:protocol>/default/<int:action>', methods=['POST'])
def set_default_action(request, protocol, action):
    try:
        if protocol not in PROTOCOLS:
          return {"error": "Invalid protocol given"}, 400

        # TODO: fill this out
        # lions_firewall.filter_default_action_set(default_action)
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
            return {"error": "Invalid protocol given"}, 400

        if _filter != 0 and _filter != 1:
            return {"error": "Invalid filter given"}, 400

        rule_id = lions_firewall.rule_add(protocol, _filter, src_ip, src_port, src_subnet,
                                          dest_ip, dest_port, dest_subnet, action)
        new_rule = {"id": rule_id}
        return {"status": "ok", "rule": new_rule}, 201
    except Exception as e:
        return {"error": "Invalid input"}, 400


@app.route('/api/rules/<string:protocol>/<int:rule_id>', methods=['DELETE'])
def delete_rule(request, protocol, rule_id):
    try:
        if protocol not in PROTOCOLS:
            return {"error": "Invalid protocol given"}, 400

        lions_firewall.rule_delete(rule_id)
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
      <a href="/">Home</a> | <a href="/config">Config</a> | <a href="/rules">Rules</a> | <a href="/interface">Interface</a>
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
      <a href="/">Home</a> | <a href="/config">Config</a> | <a href="/rules">Rules</a> | <a href="/interface">Interface</a>
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

@app.route('/config')
def config(request):
    html = """
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8">
    <title>Config Page</title>
    <link rel="stylesheet" href="/main.css">
  </head>
  <body>
    <h1>Configuration Page</h1>
    <nav>
      <a href="/">Home</a> | <a href="/config">Config</a> | <a href="/rules">Rules</a> | <a href="/interface">Interface</a>
    </nav>
    
    <h2>Interfaces</h2>
    <table border="1" id="config-table">
      <thead>
        <tr>
          <th>Interface</th>
          <th>MAC Address</th>
          <th>Current Network (CIDR)</th>
          <th>New CIDR</th>
          <th>Actions</th>
        </tr>
      </thead>
      <tbody id="config-body">
        <tr>
          <td colspan="5">Loading interface data...</td>
        </tr>
      </tbody>
    </table>
    
    <h2>Routing Table</h2>
    <table border="1">
      <thead>
        <tr>
          <th>ID</th>
          <th>Destination</th>
          <th>Gateway</th>
          <th>Interface</th>
          <th>Actions</th>
        </tr>
      </thead>
      <tbody id="routes-body">
        <tr>
          <td colspan="5">Loading routes...</td>
        </tr>
      </tbody>
    </table>
    
    <h3>Add New Route</h3>
    <p>
      Destination (CIDR): <input type="text" id="new-destination" placeholder="e.g. 10.0.0.0/8"><br>
      Gateway: <input type="text" id="new-gateway" placeholder="Gateway IP (or leave blank)"><br>
      Interface: <input type="number" id="new-interface" placeholder="Interface index"><br>
      <button id="add-route-btn">Add Route</button>
    </p>
    
    <script>
      document.addEventListener("DOMContentLoaded", function() {
        var configBody = document.getElementById('config-body');
        configBody.innerHTML = "";
        fetch('/api/interfaces/count')
          .then(function(response) { return response.json(); })
          .then(function(data) {
            for (let i = 0; i < data.count; i++) {
              fetch('/api/interfaces/' + i)
                .then(function(response) { return response.json(); })
                .then(function(info) {
                  let row = document.createElement('tr');
                  
                  let cellInterface = document.createElement('td');
                  cellInterface.textContent = info.interface;
                  row.appendChild(cellInterface);
                  
                  let cellMac = document.createElement('td');
                  cellMac.textContent = info.mac;
                  row.appendChild(cellMac);
                  
                  let cellCurrent = document.createElement('td');
                  cellCurrent.textContent = info.cidr;
                  row.appendChild(cellCurrent);
                  
                  let cellInput = document.createElement('td');
                  let input = document.createElement('input');
                  input.type = "text";
                  input.value = info.cidr;
                  input.id = "cidr-input-" + info.interface;
                  cellInput.appendChild(input);
                  row.appendChild(cellInput);
                  
                  let cellActions = document.createElement('td');
                  let btn = document.createElement('button');
                  btn.textContent = "Update";
                  btn.addEventListener("click", function() {
                    let newCidr = document.getElementById("cidr-input-" + info.interface).value;
                    fetch('/api/interfaces/' + info.interface + '/network', {
                      method: 'PUT',
                      headers: { 'Content-Type': 'application/json' },
                      body: JSON.stringify({ cidr: newCidr })
                    })
                    .then(function(response) {
                      if (!response.ok) throw new Error('Update failed');
                      return response.json();
                    })
                    .then(function(result) {
                      alert("Interface " + info.interface + " updated to " + result.cidr);
                      cellCurrent.textContent = result.cidr;
                    })
                    .catch(function(error) {
                      alert("Error updating interface " + info.interface);
                    });
                  });
                  cellActions.appendChild(btn);
                  row.appendChild(cellActions);
                  
                  configBody.appendChild(row);
                })
                .catch(function(err) {
                  let row = document.createElement('tr');
                  row.innerHTML = "<td colspan='5'>Error retrieving interface " + i + "</td>";
                  configBody.appendChild(row);
                });
            }
          })
          .catch(function(err) {
            configBody.innerHTML = "<tr><td colspan='5'>Error retrieving interface count</td></tr>";
          });

        function loadRoutes() {
          var routesBody = document.getElementById('routes-body');
          routesBody.innerHTML = "";
          fetch('/api/routes')
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
                  
                  let cellGateway = document.createElement('td');
                  cellGateway.textContent = route.gateway ? route.gateway : "-";
                  row.appendChild(cellGateway);
                  
                  let cellIface = document.createElement('td');
                  cellIface.textContent = route.interface;
                  row.appendChild(cellIface);
                  
                  let cellActions = document.createElement('td');
                  let delBtn = document.createElement('button');
                  delBtn.textContent = "Delete";
                  delBtn.addEventListener("click", function() {
                    fetch('/api/routes/' + route.id, { method: 'DELETE' })
                      .then(function(response) {
                        if (!response.ok) throw new Error("Delete failed");
                        return response.json();
                      })
                      .then(function(result) {
                        alert("Route " + route.id + " deleted.");
                        loadRoutes();
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
        loadRoutes();
        
        document.getElementById('add-route-btn').addEventListener('click', function() {
          var destination = document.getElementById('new-destination').value;
          var gateway = document.getElementById('new-gateway').value;
          var iface = document.getElementById('new-interface').value;
          fetch('/api/routes', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ destination: destination, gateway: gateway, interface: parseInt(iface) })
          })
          .then(function(response) {
            if (!response.ok) throw new Error('Add route failed');
            return response.json();
          })
          .then(function(result) {
            alert("Route added successfully.");
            loadRoutes();
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
      <a href="/">Home</a> | <a href="/config">Config</a> | <a href="/rules">Rules</a> | <a href="/interface">Interface</a>
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
          fetch('/api/rules/INSERT_PROTOCOL')
            .then(function(response) { return response.json(); })
            .then(function(data) {
              for (let i = 0; i < defaultAction.options.length; i++) {
                if (data.default_action == defaultAction.options[i].value) {
                  defaultAction.options[i].selected = true;
                }
              }
              if (data.rules.length === 0) {
                var row = document.createElement('tr');
                row.innerHTML = "<td colspan='5'>No rules available</td>";
                rulesBody.appendChild(row);
              } else {
                data.rules.forEach(function(rule) {
                  var row = document.createElement('tr');
                  row.innerHTML = "<td>" + rule.id + "</td>" +
                                  "<td class='monospace'>" + rule.src_ip + "</td>" +
                                  "<td class='monospace'>" + rule.src_port + "</td>" +
                                  "<td class='monospace'>" + rule.dest_ip + "</td>" +
                                  "<td class='monospace'>" + rule.dest_port + "</td>" +
                                  "<td class='monospace'>" + rule.src_subnet + "</td>" +
                                  "<td class='monospace'>" + rule.dest_subnet + "</td>" +
                                  "<td>" + rule.action + "</td>" +
                                  "<td><button onclick='deleteRule(" + rule.id + ")'>Delete</button></td>";
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

        window.deleteRule = function(ruleId) {
          fetch('/api/rules/INSERT_PROTOCOL' + ruleId, {
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
          fetch(`/api/rules/INSERT_PROTOCOL/default/${newDefaultAction}`, {
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
          fetch(`/api/rules/INSERT_PROTOCOL/default/${newDefaultAction}`, {
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
            filter = 0;
          } else if (filterExternal) {
            filter = 1;
          } else {
            alert("uh oh");
            return;
          }
          var srcIp = document.getElementById('new-src-ip').value;
          var srcPort = document.getElementById('new-src-port').value;
          var srcSubnet = document.getElementById('new-src-subnet').value;
          var destIp = document.getElementById('new-dest-ip').value;
          var destPort = document.getElementById('new-dest-port').value;
          var destSubnet = document.getElementById('new-dest-subnet').value;
          var action = document.getElementById('new-action').value;
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
      <a href="/">Home</a> | <a href="/config">Config</a> | <a href="/rules">Rules</a> | <a href="/interface">Interface</a>
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