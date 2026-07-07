extends Node

var server = TCPServer.new()
#Create a TCP Server
@export var Server_Address : LineEdit
@export var Server_Port : LineEdit
# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	pass # Replace with function body.


# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(_delta: float) -> void:
	if server.is_listening():
		var socket = server.take_connection()
		if socket:
			print(socket.get_var())
	pass


func _on_create_server_button_down() -> void:
	_CreatServer("127.0.0.1",8080)
	pass # Replace with function body.

func _CreatServer(ip,port) -> void:
	server.listen(port,ip)
	print("Server running on ",ip," : ",port)
	pass

func _on_connect_button_down() -> void:
	var ServerAddress = Server_Address.text
	var ServerPort = Server_Port.text
	ServerPort = ServerPort.to_int()
	print("Connecting to",'"',ServerAddress," : ",ServerPort)
	
	pass # Replace with function body.

func _Connect_to_server(ip,port) -> void:
	var client = StreamPeerTCP.new()
	client.connect_to_host(ip,port)
	if !client.connect_to_host(ip,port):
		client.put_var("Connected!")
		print("Connected to Server!")
	pass
