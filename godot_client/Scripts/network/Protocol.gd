extends Node
class_name Protocol

# --- Signals ---
# Follows the same protocol as the existing Python client (client.py).

# --- Signals ---
signal game_started(game_data, self_info)
signal tile_drawn(who, tile_id)
signal tile_discarded(who, tile_id, mode)
signal action_requested(title, actions)
signal select_tile_requested(tiles, banned, riichi)
signal meld_performed(event_type, action)
signal riichi_declared(action)
signal agari(action, ura_dora)
signal ryuukyoku(data)
signal settlement(data)
signal score_update(scores)
signal game_ended(message)
signal state_updated(key, value)

# --- Members ---
var websocket = null
var _game_state_ref = null

func setup(ws, game_state) -> void:
	websocket = ws
	_game_state_ref = weakref(game_state)
	ws.message_received.connect(_on_message)

func _on_message(event: String, data: Dictionary) -> void:
	match event:
		"join":
			print("Join result: ", data.get("message", ""))

		"start":
			var game_data = data.get("game", {})
			var self_info = data.get("self", {})
			game_started.emit(game_data, self_info)

		"draw":
			draw_handle(data)

		"select_tile":
			select_tile_handle(data)

		"discard":
			discard_handle(data)

		"decision":
			decision_handle(data)

		"chi":
			meld_performed.emit("chi", data.get("action", {}))

		"pon":
			meld_performed.emit("pon", data.get("action", {}))

		"kan":
			meld_performed.emit("kan", data.get("action", {}))

		"riichi":
			riichi_declared.emit(data.get("action", {}))

		"agari":
			agari_handle(data)

		"ryuukyoku":
			ryuukyoku.emit(data)

		"settlement":
			settlement.emit(data)

		"score":
			score_update.emit(data.get("score", []))

		"end":
			game_ended.emit(data.get("message", ""))

		"update":
			state_updated.emit(data.get("key", ""), data.get("value"))

		"wait":
			pass

		_:
			print("Unknown event: ", event, " data: ", data)


func draw_handle(data: Dictionary) -> void:
	var who = data.get("who", -1)
	var tile_id = data.get("tile_id", -1)
	tile_drawn.emit(who, tile_id)


func select_tile_handle(data: Dictionary) -> void:
	var tiles_data = data.get("tiles", [])
	var banned = data.get("banned", [])
	var riichi = data.get("riichi", false)

	var tiles_arr = []
	if typeof(tiles_data) == TYPE_STRING and tiles_data == "all":
		pass  # will resolve via game state
	elif typeof(tiles_data) == TYPE_ARRAY:
		tiles_arr = tiles_data

	select_tile_requested.emit(tiles_arr, banned, riichi)


func discard_handle(data: Dictionary) -> void:
	var who = data.get("who", -1)
	var tile_id = data.get("tile_id", -1)
	var mode = data.get("mode", 0)
	tile_discarded.emit(who, tile_id, mode)


func decision_handle(data: Dictionary) -> void:
	var title = data.get("title", "")
	var actions = data.get("actions", [])
	action_requested.emit(title, actions)


func agari_handle(data: Dictionary) -> void:
	var actions = data.get("action", [])
	var ura = data.get("ura_dora_indicator", [])
	agari.emit(actions, ura)


# --- Send helpers ---
func send_discard(tile_id: int) -> void:
	if websocket:
		websocket.send_message({
			"event": "discard",
			"tile_id": tile_id
		})


func send_decision(action: Dictionary) -> void:
	if websocket:
		websocket.send_message({
			"event": "decision",
			"action": action
		})


func send_ready() -> void:
	if websocket:
		websocket.send_message({"event": "ready"})
