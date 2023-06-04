// Establish server on the host, that the browser connects to
var server = `ws://${window.location.hostname}/ws`;
// This server is the argument for the new WebSocket object "connection"
var connection = new WebSocket(server);

var scannedList = document.getElementById("scannedList");
var enrolledList = document.getElementById("enrolledList");

// Print messages on WebSocket events: 
connection.onopen = function () {
	console.log("Connected to server on: ", server);
};
connection.onclose = function() {
	console.log("Disconnected from server on: ", server);
};
connection.onerror = function (error) {
	console.log("WebSocket Error ", error);
	alert("WebSocket Error ", error);
};
// Handle received message from server:
connection.onmessage = function (msg) {
	console.log("Received: ", msg);
	// Parse the JSON string received to valid data:
	let data = JSON.parse(msg.data);

	// If the data contains a "scanned" element:
	if(data.scanned){
		let cardId = data.scanned;

		// Check if card already scanned:
		if(document.getElementById(cardId)){
			return;
		}
		else{
		// Else add the card to list:
		let div = document.createElement("div");
		div.setAttribute("class", "card");
		div.setAttribute("id", cardId);
		div.innerHTML = `${cardId}`
		scannedList.appendChild(div);

		let button = document.createElement("button");
		button.setAttribute("class", "button green"); 
		button.innerHTML = "Enroll"
		button.onclick = function(){enrollThis(cardId);};
		div.appendChild(button);
		}
	}

	if(data.enrolled){
		// Clear old list:
		enrolledList.textContent = "";
		// Populate enrolled list:
		data.enrolled.forEach(cardId => {
			let div = document.createElement("div");
			div.setAttribute("class", "card");
			div.setAttribute("id", cardId);
			div.innerHTML = `${cardId}`
			enrolledList.appendChild(div);

			let button = document.createElement("button");
			button.setAttribute("class", "button red"); 
			button.innerHTML = "Delete"
			button.onclick = function(){deleteThis(cardId);};
			div.appendChild(button);
		});
	}
};

// Callback to delete card on ESP32 over websocket:
function deleteThis(cardId){
	// Construct JSON data:
	let data = { delete: cardId };

	// Remove DOM node:
	document.getElementById(cardId).remove();

	// Send update:
	let dataStr = JSON.stringify(data);
	connection.send(dataStr);
	console.log("sending: ",dataStr);
}

// Callback to enroll card on ESP32 over websocket:
function enrollThis(cardId){
	// Construct JSON data:
	let data = { enroll: cardId };

	// Remove DOM node:
	document.getElementById(cardId).remove();

	// Send update:
	let dataStr = JSON.stringify(data);
	connection.send(dataStr);
	console.log("sending: ",dataStr);
}