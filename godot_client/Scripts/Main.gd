extends Control
# Main.gd — game entry point. Builds menu + game table UI via scene nodes.
# Communicates with C++ backend over WebSocket + NDJSON protocol.

@onready var menu_panel = $MenuPanel
@onready var game_panel = $GamePanel
@onready var status_label = $MenuPanel/VBox/Status
@onready var hand_tiles_cont = $GamePanel/HandPanel/HandTiles
@onready var action_label = $GamePanel/ActionPanel/ActionLabel
@onready var action_buttons = $GamePanel/ActionPanel/ActionButtons
@onready var round_label = $GamePanel/RoundLabel
@onready var dora_label = $GamePanel/DoraLabel
@onready var left_label = $GamePanel/LeftNumLabel
@onready var opp_info = $GamePanel/OpponentsPanel/OppInfo
@onready var scores_panel = $GamePanel/ScoresPanel

var _ws: WebSocketClient
var _protocol: Protocol
var _game_state: GameState
var _tile_buttons: Array = []    # Button nodes for hand tiles
var _pending_tiles: Array = []   # currently selectable tiles
var _pending_tsumo: int = -1     # just-drawn tile id

# ---------------------------------------------------------------
# Init
# ---------------------------------------------------------------
func _ready() -> void:
	_game_state = GameState.new()
	add_child(_game_state)
	
	_ws = WebSocketClient.new()
	add_child(_ws)
	
	_protocol = Protocol.new()
	add_child(_protocol)
	_protocol.setup(_ws, _game_state)
	
	# Connect signals
	_ws.connected.connect(_on_ws_connected)
	_ws.disconnected.connect(_on_ws_disconnected)
	
	_protocol.game_started.connect(_on_game_started)
	_protocol.select_tile_requested.connect(_on_select_tile)
	_protocol.action_requested.connect(_on_action_requested)
	_protocol.tile_drawn.connect(_on_tile_drawn)
	_protocol.tile_discarded.connect(_on_tile_discarded)
	_protocol.meld_performed.connect(_on_meld)
	_protocol.riichi_declared.connect(_on_riichi)
	_protocol.agari.connect(_on_agari)
	_protocol.ryuukyoku.connect(_on_ryuukyoku)
	_protocol.settlement.connect(_on_settlement)
	_protocol.game_ended.connect(_on_game_ended)
	_protocol.state_updated.connect(_on_state_updated)
	
	# Connect button
	$MenuPanel/VBox/ConnectBtn.pressed.connect(_on_connect_pressed)

# ---------------------------------------------------------------
# Connect / disconnect
# ---------------------------------------------------------------
func _on_connect_pressed() -> void:
	var addr = $MenuPanel/VBox/AddrHBox/ServerAddress.text
	var port_str = $MenuPanel/VBox/PortHBox/ServerPort.text
	var username = $MenuPanel/VBox/UserHBox/Username.text
	var port = int(port_str)
	
	status_label.text = "Connecting..."
	_ws.connect_to_server(addr, port)
	
	await _ws.connected
	
	_ws.send_message({
		"username": username,
		"observe": false
	})
	status_label.text = "Connected! Waiting for game..."

func _on_ws_connected() -> void:
	print("Connected to server!")

func _on_ws_disconnected() -> void:
	status_label.text = "Disconnected"
	menu_panel.visible = true
	game_panel.visible = false

# ---------------------------------------------------------------
# Game start
# ---------------------------------------------------------------
func _on_game_started(game_data, self_info) -> void:
	_game_state.on_game_start(game_data, self_info)
	
	menu_panel.visible = false
	game_panel.visible = true
	_clear_tile_buttons()
	_clear_action_buttons()
	
	_refresh_round()
	_refresh_scores()
	_refresh_opponents()
	_refresh_hand()

# ---------------------------------------------------------------
# Tile draw / discard
# ---------------------------------------------------------------
func _on_tile_drawn(who, tile_id) -> void:
	_game_state.on_draw(who, tile_id)
	if who == _game_state.my_seat:
		_pending_tsumo = tile_id
		_refresh_hand()

func _on_tile_discarded(who, tile_id, mode) -> void:
	_game_state.on_discard(who, tile_id, mode)
	_refresh_hand()
	_refresh_scores()
	_refresh_opponents()

# ---------------------------------------------------------------
# Select tile (our turn to discard)
# ---------------------------------------------------------------
func _on_select_tile(tiles, banned, riichi) -> void:
	# Build clickable tile buttons
	_clear_tile_buttons()
	
	var my_tiles = _game_state.my_tiles.duplicate()
	my_tiles.sort()
	_pending_tiles = my_tiles
	
	for idx in range(my_tiles.size()):
		var tile_id = my_tiles[idx]
		var btn = Button.new()
		btn.text = GameState.tile_name(tile_id)
		btn.name = "Tile_%d" % idx
		btn.custom_minimum_size = Vector2(52, 52)
		btn.pressed.connect(_on_tile_clicked.bind(idx, tile_id))
		hand_tiles_cont.add_child(btn)
		_tile_buttons.append(btn)
	
	action_label.text = "你的回合 — 点击手牌切出"

func _on_tile_clicked(idx: int, tile_id: int) -> void:
	if _pending_tiles.size() == 0:
		return
	
	_clear_tile_buttons()
	action_label.text = ""
	
	# Send discard to server
	_protocol.send_discard(tile_id)

func _clear_tile_buttons() -> void:
	for btn in _tile_buttons:
		btn.queue_free()
	_tile_buttons.clear()
	_pending_tiles.clear()

# ---------------------------------------------------------------
# Action / decision
# ---------------------------------------------------------------
func _on_action_requested(title, actions) -> void:
	_clear_action_buttons()
	
	var text = "操作选择:"
	for i in range(actions.size()):
		var act = actions[i]
		var act_type = act.get("type", "?")
		text += " [%d]%s" % [i, act_type]
	action_label.text = text
	
	# Build action buttons
	for i in range(actions.size()):
		var act = actions[i]
		var type_str = act.get("type", "?")
		var btn = Button.new()
		btn.text = "%d: %s" % [i, type_str]
		btn.custom_minimum_size = Vector2(90, 36)
		btn.pressed.connect(_on_action_clicked.bind(i, act))
		action_buttons.add_child(btn)

func _on_action_clicked(idx: int, act: Dictionary) -> void:
	_clear_action_buttons()
	action_label.text = ""
	_protocol.send_decision(act)

func _clear_action_buttons() -> void:
	for child in action_buttons.get_children():
		child.queue_free()

# ---------------------------------------------------------------
# Meld / Riichi / Agari / Ryuukyoku
# ---------------------------------------------------------------
func _on_meld(event_type, action) -> void:
	_game_state.on_meld(event_type, action)
	_refresh_hand()
	_refresh_opponents()
	_refresh_scores()
	_clear_tile_buttons()
	_clear_action_buttons()

func _on_riichi(action) -> void:
	_game_state.on_riichi(action)
	_refresh_scores()
	_refresh_opponents()

func _on_agari(actions, ura_dora) -> void:
	_game_state.on_agari(actions, ura_dora)
	_clear_tile_buttons()
	_clear_action_buttons()
	action_label.text = "和牌！"

func _on_ryuukyoku(data) -> void:
	_game_state.on_ryuukyoku(data)
	_clear_tile_buttons()
	_clear_action_buttons()
	action_label.text = "流局: " + data.get("why", "")

func _on_settlement(data) -> void:
	_game_state.on_settlement(data)
	_clear_tile_buttons()
	_clear_action_buttons()
	action_label.text = "局终结算"
	_refresh_scores()

func _on_game_ended(message) -> void:
	_clear_tile_buttons()
	_clear_action_buttons()
	action_label.text = "游戏结束: " + message

func _on_state_updated(key, value) -> void:
	match key:
		"furiten":
			_game_state.furiten = value
		"machi":
			_game_state.my_machi = value
		"left_num":
			_game_state.left_num = value
			left_label.text = "余牌: %d" % value

# ---------------------------------------------------------------
# UI refresh helpers
# ---------------------------------------------------------------
func _refresh_round() -> void:
	var wind = _game_state.get_round_wind()
	var num = _game_state.get_round_number()
	round_label.text = "%s%d局 %d本场" % [wind, num, _game_state.honba]
	
	var dora_text = "宝牌: "
	for d in _game_state.dora_indicators:
		dora_text += GameState.tile_name(d) + " "
	dora_label.text = dora_text
	left_label.text = "余牌: %d" % _game_state.left_num

func _refresh_scores() -> void:
	for i in range(4):
		var label = scores_panel.get_node("Score%d" % i)
		if not label:
			continue
		var agent = _game_state.get_agent_info(i)
		var name = agent.get("username", "?")
		var score = agent.get("score", 250) * 100
		var wind_str = _game_state.get_wind_name(i)
		var riichi_mark = " R" if agent.get("riichi", 0) == 1 else ""
		var you = " <-" if i == _game_state.my_seat else ""
		label.text = "%s %s: %d%s%s" % [wind_str, name, score, riichi_mark, you]

func _refresh_opponents() -> void:
	var text = ""
	for i in range(4):
		if i == _game_state.my_seat:
			continue
		var agent = _game_state.get_agent_info(i)
		var name = agent.get("username", "?")
		var tile_count = agent.get("tile_count", 13)
		var wind_str = _game_state.get_wind_name(i)
		var riichi_mark = " R" if agent.get("riichi", 0) == 1 else ""
		
		# Show river
		var disc = agent.get("discard", [])
		var river_text = ""
		var max_show = min(disc.size(), 12)
		for j in range(max(disc.size()) - max_show, disc.size()):
			river_text += GameState.tile_name(disc[j]) + " "
		
		text += "%s %s (%d枚)%s\n  河: %s\n" % [wind_str, name, tile_count, riichi_mark, river_text]
	
	opp_info.text = text

func _refresh_hand() -> void:
	if _game_state.my_tiles.size() == 0:
		return
	_clear_tile_buttons()
	
	var tiles = _game_state.my_tiles.duplicate()
	tiles.sort()
	
	for tile_id in tiles:
		var btn = Button.new()
		btn.text = GameState.tile_name(tile_id)
		btn.custom_minimum_size = Vector2(52, 52)
		btn.disabled = true  # read-only display between turns
		hand_tiles_cont.add_child(btn)
		_tile_buttons.append(btn)
	
	# Show furo as well
	if _game_state.my_furo.size() > 0:
		var furo_text = ""
		for key in _game_state.my_furo:
			var tiles_f = _game_state.my_furo[key]
			for t in tiles_f:
				furo_text += "[" + GameState.tile_name(t) + "] "
		action_label.text = "副露: " + furo_text
