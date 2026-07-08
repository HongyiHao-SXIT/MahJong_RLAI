# WebSocketClient.gd
# Handles WebSocket connection to C++ backend server.
# Protocol: NDJSON over WebSocket (same as existing Python client)

extends Node
class_name WebSocketClient

signal connected()
signal disconnected()
signal message_received(event: String, data: Dictionary)

const RECONNECT_DELAY := 2.0
const MAX_RECONNECT_ATTEMPTS := 5

var _socket := WebSocketPeer.new()
var _url := ""
var _reconnect_count := 0
var _auto_reconnect := false
var _connected := false

# Connect to server
func connect_to_server(host: String, port: int) -> void:
	_url = "ws://%s:%d" % [host, port]
	_reconnect_count = 0
	_auto_reconnect = true
	_do_connect()

func _do_connect() -> void:
	if _socket.get_ready_state() != WebSocketPeer.STATE_CLOSED:
		_socket.close()
	_socket = WebSocketPeer.new()
	
	var err = _socket.connect_to_url(_url)
	if err != OK:
		push_error("WebSocket connect failed: ", err)
		_schedule_reconnect()
		return
	
	print("Connecting to ", _url)

func disconnect_from_server() -> void:
	_auto_reconnect = false
	_connected = false
	if _socket.get_ready_state() == WebSocketPeer.STATE_OPEN:
		_socket.close()

func send_message(data: Dictionary) -> void:
	if _socket.get_ready_state() != WebSocketPeer.STATE_OPEN:
		push_warning("Cannot send: not connected")
		return
	var json_str = JSON.stringify(data)
	_socket.send_text(json_str)

func send_raw(json_str: String) -> void:
	if _socket.get_ready_state() != WebSocketPeer.STATE_OPEN:
		return
	_socket.send_text(json_str)

func _process(_delta: float) -> void:
	_socket.poll()
	
	var state = _socket.get_ready_state()
	
	if state == WebSocketPeer.STATE_OPEN and not _connected:
		_connected = true
		_reconnect_count = 0
		print("WebSocket connected!")
		connected.emit()
	
	if state == WebSocketPeer.STATE_CLOSING:
		pass
	elif state == WebSocketPeer.STATE_CLOSED:
		if _connected:
			_connected = false
			print("WebSocket disconnected")
			disconnected.emit()
			if _auto_reconnect:
				_schedule_reconnect()
		_connected = false
	
	# Read messages
	while state == WebSocketPeer.STATE_OPEN and _socket.get_available_packet_count() > 0:
		var packet = _socket.get_packet()
		if packet.size() == 0:
			continue
		var text = packet.get_string_from_utf8()
		if text.is_empty():
			continue
		_parse_message(text)

func _parse_message(text: String) -> void:
	var json = JSON.new()
	var err = json.parse(text)
	if err != OK:
		push_error("JSON parse error: ", json.get_error_message(), " text: ", text)
		return
	
	var data = json.data as Dictionary
	if not data.has("event"):
		push_warning("Message without event: ", text)
		return
	
	var event_name = data["event"] as String
	message_received.emit(event_name, data)

func _schedule_reconnect() -> void:
	if _reconnect_count >= MAX_RECONNECT_ATTEMPTS:
		push_error("Max reconnect attempts reached")
		return
	_reconnect_count += 1
	print("Reconnecting in ", RECONNECT_DELAY, "s (attempt ", _reconnect_count, ")")
	await get_tree().create_timer(RECONNECT_DELAY).timeout
	if _auto_reconnect:
		_do_connect()
