// Establish server on the host, that the browser connects to
var server = `ws://${window.location.hostname}/ws`;
// This server is the argument for the new WebSocket object "connection"
var connection = new WebSocket(server);

var latestPictures = document.getElementById("latestPictures");

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
	try {
        let data = JSON.parse(msg.data);
		if(data.photos){
			addImagesDelay(data.photos)
		}
    } catch (e) {
        recvImage(msg.data);
    }
}

// Define a function to receive image binary stream over websocket:
async function recvImage(image){
	// Cast encoded data to binary:
	let imageArrayBuf = await image.arrayBuffer();
	let imageBinBuf = new Uint8Array(imageArrayBuf);
	// Print encoded:
	console.log("Binary: ", imageBinBuf);
	let base64EncodedStr = btoa(String.fromCharCode.apply(null, imageBinBuf));
	console.log("Base64: ", base64EncodedStr);
	
	// Get JPEG binary data and display image:
	let img = document.createElement('img');
	console.log("Base64: ", base64EncodedStr);
	imageToDOM(("data:image/jpeg;base64," + base64EncodedStr), latestPictures);
	latestPictures.appendChild(img);
}

// Add images to DOM with delay (To avoid crashing ESP32-CAM):
async function addImagesDelay(images){
	for (let i = 0; i < images.length; i++){
		imageToDOM(images[i], latestPictures);
		delay = new Promise(resolve => {
			setTimeout(resolve, 750);
		});
		await delay;
	}
}

// Helper for adding an image to DOM:
function imageToDOM(src, parent){
	let img = document.createElement("img");
	img.setAttribute("src", src);
	img.setAttribute("width", "20%");
	img.setAttribute("height", "20%");
	img.setAttribute("object-fit", "contain");
	parent.appendChild(img);
}