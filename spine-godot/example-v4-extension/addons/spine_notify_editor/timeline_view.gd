@tool
extends Control

var dock

func setup(p_dock) -> void:
	dock = p_dock

func refresh() -> void:
	queue_redraw()
