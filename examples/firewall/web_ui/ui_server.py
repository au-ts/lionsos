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

@app.route('/api/rules', methods=['GET'])
def get_rules(request):
    rules = []
    for i in range(lions_firewall.rule_count()):
        t = lions_firewall.rule_get_nth(i)
        rules.append({
            "id": t[0],
            "protocol": t[1],
            "iface1": t[2],
            "iface2": t[3]
        })
    rules.sort(key=lambda rule: rule['id'])
    return {"rules": rules}

@app.route('/api/rules', methods=['POST'])
def add_rule(request):
    try:
        data = request.json
        protocol = data.get("protocol")
        iface1 = data.get("iface1")
        iface2 = data.get("iface2")
        if protocol is None or iface1 is None or iface2 is None:
            return {"error": "Missing protocol or interface"}, 400
        rule_id = lions_firewall.rule_add(protocol, iface1, iface2)
        new_rule = {"id": rule_id, "protocol": protocol, "iface1": iface1, "iface2": iface2}
        return {"status": "ok", "rule": new_rule}, 201
    except Exception as e:
        return {"error": "Invalid input"}, 400

@app.route('/api/rules/<int:rule_id>', methods=['DELETE'])
def delete_rule(request, rule_id):
    try:
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
  </head>
  <body>
    <h1>Firewall Configuration</h1>
    <nav>
      <a href="/config">Config</a> | <a href="/rules">Rules</a>
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
  </head>
  <body>
    <h1>Configuration Page</h1>
    <nav>
      <a href="/">Home</a> | <a href="/rules">Rules</a>
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

@app.route('/rules')
def rules(request):
    html = """
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8">
    <title>Firewall Rules</title>
  </head>
  <body>
    <h1>Firewall Rules</h1>
    <nav>
      <a href="/">Home</a> | <a href="/config">Config</a>
    </nav>
    
    <h2>Existing Rules</h2>
    <table border="1">
      <thead>
        <tr>
          <th>ID</th>
          <th>Protocol</th>
          <th>IFACE 1</th>
          <th>IFACE 2</th>
          <th>Actions</th>
        </tr>
      </thead>
      <tbody id="rules-body">
        <tr><td colspan="5">Loading rules...</td></tr>
      </tbody>
    </table>
    
    <h2>Add New Rule</h2>
    <p>
      Protocol: <input type="text" id="new-protocol" placeholder="e.g. TCP"><br>
      IFACE 1: <input type="text" id="new-iface1" placeholder="e.g. Anywhere or 192.168.10.3/24"><br>
      IFACE 2: <input type="text" id="new-iface2" placeholder="e.g. 123.456.788.23:80"><br>
      <button id="add-rule-btn">Add Rule</button>
    </p>
    
    <script>
      document.addEventListener("DOMContentLoaded", function() {
        function loadRules() {
          var rulesBody = document.getElementById('rules-body');
          rulesBody.innerHTML = "";
          fetch('/api/rules')
            .then(function(response) { return response.json(); })
            .then(function(data) {
              if (data.rules.length === 0) {
                var row = document.createElement('tr');
                row.innerHTML = "<td colspan='5'>No rules available</td>";
                rulesBody.appendChild(row);
              } else {
                data.rules.forEach(function(rule) {
                  var row = document.createElement('tr');
                  row.innerHTML = "<td>" + rule.id + "</td>" +
                                  "<td>" + rule.protocol + "</td>" +
                                  "<td>" + rule.iface1 + "</td>" +
                                  "<td>" + rule.iface2 + "</td>" +
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
          fetch('/api/rules/' + ruleId, {
            method: 'DELETE',
            headers: { 'Content-Type': 'application/json' }
          })
          .then(function(response) {
            if (!response.ok) throw new Error("Delete failed");
            return response.json();
          })
          .then(function(result) {
            alert("Rule " + ruleId + " deleted.");
            loadRules();
          })
          .catch(function(error) {
            alert("Error deleting rule " + ruleId);
          });
        }
        
        document.getElementById('add-rule-btn').addEventListener('click', function() {
          var protocol = document.getElementById('new-protocol').value;
          var iface1 = document.getElementById('new-iface1').value;
          var iface2 = document.getElementById('new-iface2').value;
          fetch('/api/rules', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ protocol: protocol, iface1: iface1, iface2: iface2 })
          })
          .then(function(response) {
            if (!response.ok) throw new Error("Add rule failed");
            return response.json();
          })
          .then(function(result) {
            alert("Rule added successfully.");
            loadRules();
          })
          .catch(function(err) {
            alert("Error adding rule");
          });
        });
        
        loadRules();
      });
    </script>
  </body>
</html>
"""
    return Response(body=html, headers={'Content-Type': 'text/html'})

app.run(debug=True, port=80)
