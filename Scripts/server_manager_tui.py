#!/usr/bin/env python3
"""
Zero-dependency terminal UI for local Mession server orchestration.
"""

from __future__ import annotations

import argparse
import curses
import time
from pathlib import Path
from typing import Optional

from server_cluster import BaseNodeController, ClusterManager, NodeRegistryEntry
from server_control_api import resolve_build_dir


DEFAULT_REFRESH_INTERVAL = 0.5
MIN_HEIGHT = 20
MIN_WIDTH = 80
MAX_TAIL_READ_LINES = 4000
TABS = ["Overview", "Logs", "Tasks", "Fleet", "Help"]
FLEET_GROUP_MODES = ["custom", "state", "mode", "topology"]


def clip_text(text: str, width: int) -> str:
    if width <= 0:
        return ""
    if len(text) <= width:
        return text
    if width == 1:
        return text[:1]
    return text[: width - 1] + ">"


def safe_addstr(window: curses.window, y: int, x: int, text: str, attr: int = 0) -> None:
    height, width = window.getmaxyx()
    if y < 0 or y >= height or x < 0 or x >= width:
        return
    text = clip_text(text, width - x)
    if not text:
        return
    try:
        window.addstr(y, x, text, attr)
    except curses.error:
        pass


def draw_border(window: curses.window, title: str) -> None:
    window.box()
    safe_addstr(window, 0, 2, f" {title} ", curses.A_BOLD)


def slice_visible_lines(lines: list[str], height: int, follow: bool, offset: int) -> tuple[list[str], int]:
    if height <= 0:
        return [], 0
    max_offset = max(0, len(lines) - height)
    if follow:
        offset = 0
    else:
        offset = min(max(0, offset), max_offset)
    start = max(0, len(lines) - height - offset)
    end = min(len(lines), start + height)
    return lines[start:end], offset


class TuiColors:
    HEADER = 0
    RUNNING = 0
    STARTING = 0
    STOPPED = 0
    FAILED = 0
    HIGHLIGHT = curses.A_REVERSE
    MUTED = 0
    TAB_ACTIVE = curses.A_BOLD | curses.A_REVERSE


class ServerManagerTui:
    def __init__(self, build_dir: Path, refresh_interval: float, cluster_config: Optional[Path]):
        self.build_dir = build_dir
        self.refresh_interval = max(0.2, refresh_interval)
        self.cluster = ClusterManager(cluster_config)
        self.selected_node_index = 0

        self.snapshot: dict = {}
        self.services: list[dict] = []
        self.tasks: list[dict] = []
        self.marked_nodes: set[str] = set()
        self.batch_result_lines: list[str] = []
        self.fleet_group_mode_index = 0
        self.fleet_filter_query = ""
        self.fleet_filter_editing = False
        self.fleet_show_marked_only = False
        self.fleet_show_issue_only = False
        self.fleet_collapsed_groups: set[str] = set()

        self.active_tab_index = 0
        self.selected_service_index = 0
        self.selected_task_index = 0

        self.log_follow = True
        self.log_scroll_offset = 0
        self.task_follow_output = True
        self.task_output_scroll_offset = 0

        self.last_log_body_height = 1
        self.last_task_output_height = 1

        self.message = "Ready"
        self.last_refresh_at = 0.0
        self.should_exit = False

    def run(self, stdscr: curses.window) -> int:
        curses.curs_set(0)
        stdscr.nodelay(True)
        stdscr.timeout(100)
        self._init_colors()
        self.refresh(force=True)

        while not self.should_exit:
            if time.time() - self.last_refresh_at >= self.refresh_interval:
                self.refresh(force=True)

            self.draw(stdscr)
            ch = stdscr.getch()
            if ch != -1:
                self.handle_input(ch)

        return 0

    def _init_colors(self) -> None:
        if not curses.has_colors():
            return
        curses.start_color()
        curses.use_default_colors()
        curses.init_pair(1, curses.COLOR_CYAN, -1)
        curses.init_pair(2, curses.COLOR_GREEN, -1)
        curses.init_pair(3, curses.COLOR_YELLOW, -1)
        curses.init_pair(4, curses.COLOR_RED, -1)
        curses.init_pair(5, curses.COLOR_BLUE, -1)

        TuiColors.HEADER = curses.color_pair(1) | curses.A_BOLD
        TuiColors.RUNNING = curses.color_pair(2) | curses.A_BOLD
        TuiColors.STARTING = curses.color_pair(3) | curses.A_BOLD
        TuiColors.STOPPED = curses.color_pair(4) | curses.A_BOLD
        TuiColors.FAILED = curses.color_pair(4) | curses.A_BOLD
        TuiColors.MUTED = curses.color_pair(5)
        TuiColors.TAB_ACTIVE = curses.color_pair(1) | curses.A_BOLD | curses.A_REVERSE

    def refresh(self, force: bool = False) -> None:
        if not force and time.time() - self.last_refresh_at < self.refresh_interval:
            return

        if not self.cluster.controllers:
            self.snapshot = {}
            self.services = []
            self.tasks = []
            self.last_refresh_at = time.time()
            return

        self.selected_node_index = min(max(0, self.selected_node_index), len(self.cluster.controllers) - 1)
        self.cluster.refresh_due(force=force, selected_node_name=self.current_node_name)
        self.selected_node_index = min(max(0, self.selected_node_index), len(self.cluster.controllers) - 1)
        self.snapshot = self.cluster.get_snapshot(self.current_node_name)
        self.services = list(self.snapshot.get("services", []))
        self.tasks = self.current_controller.list_tasks()
        self.marked_nodes.intersection_update({controller.node.name for controller in self.cluster.controllers})

        if self.services:
            self.selected_service_index = min(max(0, self.selected_service_index), len(self.services) - 1)
        else:
            self.selected_service_index = 0

        if self.tasks:
            self.selected_task_index = min(max(0, self.selected_task_index), len(self.tasks) - 1)
        else:
            self.selected_task_index = 0

        self.last_refresh_at = time.time()

    def handle_input(self, ch: int) -> None:
        if self.active_tab == "Fleet" and self.fleet_filter_editing:
            self.handle_fleet_filter_input(ch)
            return

        if ch in (ord("q"), ord("Q")):
            self.should_exit = True
            return

        if ch in (9, curses.KEY_RIGHT, ord("l")):
            self.active_tab_index = (self.active_tab_index + 1) % len(TABS)
            return
        if ch in (curses.KEY_BTAB, curses.KEY_LEFT, ord("h")):
            self.active_tab_index = (self.active_tab_index - 1) % len(TABS)
            return

        if ch == ord(","):
            self.switch_node(-1)
            return
        if ch == ord("."):
            self.switch_node(1)
            return

        if ch in (ord("1"), ord("2"), ord("3"), ord("4"), ord("5")):
            self.active_tab_index = int(chr(ch)) - 1
            return

        if self.active_tab == "Fleet":
            if ch in (curses.KEY_UP, ord("k")):
                self.move_node_within_group(-1)
                return
            if ch in (curses.KEY_DOWN, ord("j")):
                self.move_node_within_group(1)
                return
            if ch == ord("/"):
                self.fleet_filter_editing = True
                self.message = "Fleet filter: type to search, Enter to apply, Esc to cancel"
                return
            if ch == ord("g"):
                self.fleet_group_mode_index = (self.fleet_group_mode_index + 1) % len(FLEET_GROUP_MODES)
                self.ensure_fleet_selection_visible()
                self.message = f"Fleet group mode: {self.fleet_group_mode}"
                return
            if ch == ord("{"):
                self.move_group_selection(-1)
                return
            if ch == ord("}"):
                self.move_group_selection(1)
                return
            if ch == ord("m"):
                self.toggle_mark_current_node()
                return
            if ch == ord("M"):
                self.mark_current_group()
                return
            if ch == ord("o"):
                self.fleet_show_marked_only = not self.fleet_show_marked_only
                self.ensure_fleet_selection_visible()
                self.message = f"Fleet marked-only {'on' if self.fleet_show_marked_only else 'off'}"
                return
            if ch == ord("i"):
                self.fleet_show_issue_only = not self.fleet_show_issue_only
                self.ensure_fleet_selection_visible()
                self.message = f"Fleet issue-only {'on' if self.fleet_show_issue_only else 'off'}"
                return
            if ch == ord("c"):
                self.toggle_current_group_collapsed()
                return
            if ch == ord("C"):
                self.collapse_visible_groups()
                return
            if ch == ord("e"):
                self.expand_all_groups()
                return
            if ch == ord("u"):
                self.marked_nodes.clear()
                self.message = "Cleared marked nodes"
                return
            if ch == ord("z"):
                self.clear_fleet_filters()
                return
            batch_action_map = {
                ord("a"): ("start", None, "Queued start on"),
                ord("X"): ("stop", None, "Queued stop on"),
                ord("s"): ("start_server", self.selected_server_name, "Queued service start on"),
                ord("x"): ("stop_server", self.selected_server_name, "Queued service stop on"),
                ord("R"): ("restart_server", self.selected_server_name, "Queued service restart on"),
                ord("b"): ("build", None, "Queued build on"),
                ord("v"): ("validate", None, "Queued validate on"),
                ord("V"): ("validate_with_build", None, "Queued validate+build on"),
            }
            action_spec = batch_action_map.get(ch)
            if action_spec:
                action, server_name, label = action_spec
                if action.endswith("_server") and not server_name:
                    self.message = "No service selected for batch service action"
                    return
                self.run_batch_action(action, server_name, label)
                return
            if ch == ord("p"):
                self.run_batch_topology()
                return

        if ch in (curses.KEY_UP, ord("k")) and self.services:
            self.selected_service_index = max(0, self.selected_service_index - 1)
            self.log_scroll_offset = 0
            self.log_follow = True
            return

        if ch in (curses.KEY_DOWN, ord("j")) and self.services:
            self.selected_service_index = min(len(self.services) - 1, self.selected_service_index + 1)
            self.log_scroll_offset = 0
            self.log_follow = True
            return

        if ch in (ord("["),):
            self.move_task_selection(-1)
            return

        if ch in (ord("]"),):
            self.move_task_selection(1)
            return

        if ch in (ord("r"),):
            self.message = "Refreshed snapshot"
            self.refresh(force=True)
            return

        if ch == ord("f"):
            if self.active_tab == "Logs":
                self.log_follow = not self.log_follow
                if self.log_follow:
                    self.log_scroll_offset = 0
                self.message = f"Log follow {'on' if self.log_follow else 'off'}"
            elif self.active_tab == "Tasks":
                self.task_follow_output = not self.task_follow_output
                if self.task_follow_output:
                    self.task_output_scroll_offset = 0
                self.message = f"Task output follow {'on' if self.task_follow_output else 'off'}"
            return

        if ch == curses.KEY_PPAGE:
            self.scroll_active_view(+1)
            return
        if ch == curses.KEY_NPAGE:
            self.scroll_active_view(-1)
            return
        if ch == curses.KEY_HOME:
            self.jump_active_view_to_oldest()
            return
        if ch == curses.KEY_END:
            self.jump_active_view_to_latest()
            return

        action_map = {
            ord("a"): ("start", None, "Queued start all"),
            ord("X"): ("stop", None, "Queued stop all"),
            ord("s"): ("start_server", self.selected_server_name, "Queued start"),
            ord("x"): ("stop_server", self.selected_server_name, "Queued stop"),
            ord("R"): ("restart_server", self.selected_server_name, "Queued restart"),
            ord("b"): ("build", None, "Queued build"),
            ord("v"): ("validate", None, "Queued validate"),
            ord("V"): ("validate_with_build", None, "Queued validate+build"),
        }
        action_spec = action_map.get(ch)
        if action_spec:
            action, server_name, label = action_spec
            if action.endswith("_server") and not server_name:
                self.message = "No server selected"
                return
            try:
                task = self.current_controller.queue_action(action, server_name=server_name)
            except Exception as exc:
                self.message = str(exc)
                return
            self.tasks = self.current_controller.list_tasks()
            self.selected_task_index = 0
            self.task_follow_output = True
            self.task_output_scroll_offset = 0
            target = f" {server_name}" if server_name else ""
            self.message = f"{label}{target}: {task.id[:8]}"
            return

        if ch == ord("p"):
            try:
                result = self.cluster.push_topology(self.current_node_name)
            except Exception as exc:
                self.message = str(exc)
                return
            self.message = f"Pushed topology to {self.current_node_name}: v={result.get('version') or '-'}"
            self.refresh(force=True)
            return

        if ch == ord("P"):
            results = self.cluster.push_topology_all()
            failed = [name for name, item in results.items() if item.get("error")]
            self.message = "Pushed topology to all nodes" if not failed else f"Topology push errors: {', '.join(failed)}"
            self.refresh(force=True)
            return

    def switch_node(self, delta: int) -> None:
        if not self.cluster.controllers:
            return
        self.selected_node_index = (self.selected_node_index + delta) % len(self.cluster.controllers)
        self.selected_service_index = 0
        self.selected_task_index = 0
        self.log_follow = True
        self.log_scroll_offset = 0
        self.task_follow_output = True
        self.task_output_scroll_offset = 0
        self.message = f"Switched to node {self.current_node.node.name}"
        self.refresh(force=True)

    def select_node_by_name(self, node_name: str) -> None:
        for index, controller in enumerate(self.cluster.controllers):
            if controller.node.name == node_name:
                self.selected_node_index = index
                self.selected_service_index = 0
                self.selected_task_index = 0
                self.log_follow = True
                self.log_scroll_offset = 0
                self.task_follow_output = True
                self.task_output_scroll_offset = 0
                self.refresh(force=True)
                return

    def handle_fleet_filter_input(self, ch: int) -> None:
        if ch in (27,):
            self.fleet_filter_editing = False
            self.message = "Fleet filter edit cancelled"
            return
        if ch in (10, 13, curses.KEY_ENTER):
            self.fleet_filter_editing = False
            self.ensure_fleet_selection_visible()
            self.message = f"Fleet filter applied: {self.fleet_filter_query or '(none)'}"
            return
        if ch in (curses.KEY_BACKSPACE, 127, 8):
            self.fleet_filter_query = self.fleet_filter_query[:-1]
            self.message = f"Fleet filter: {self.fleet_filter_query or '(none)'}"
            return
        if 32 <= ch <= 126:
            self.fleet_filter_query += chr(ch)
            self.message = f"Fleet filter: {self.fleet_filter_query}"

    def clear_fleet_filters(self) -> None:
        self.fleet_filter_query = ""
        self.fleet_show_marked_only = False
        self.fleet_show_issue_only = False
        self.fleet_collapsed_groups.clear()
        self.ensure_fleet_selection_visible()
        self.message = "Cleared Fleet search, toggles, and collapsed groups"

    @property
    def fleet_group_mode(self) -> str:
        return FLEET_GROUP_MODES[self.fleet_group_mode_index]

    def fleet_group_values(self, entry: NodeRegistryEntry) -> list[str]:
        if self.fleet_group_mode == "state":
            return [entry.heartbeat_state]
        if self.fleet_group_mode == "mode":
            return [entry.mode]
        if self.fleet_group_mode == "topology":
            return [entry.applied_topology_version or "unapplied"]
        return list(entry.groups or ["ungrouped"])

    def entry_matches_fleet_filters(self, entry: NodeRegistryEntry) -> bool:
        if self.fleet_show_marked_only and entry.node_name not in self.marked_nodes:
            return False
        if self.fleet_show_issue_only and not (entry.topology_issues or entry.last_error):
            return False
        if not self.fleet_filter_query:
            return True

        controller = self.cluster.get_controller(entry.node_name)
        terms = [
            entry.node_name,
            entry.host,
            entry.mode,
            entry.heartbeat_state,
            entry.registry_source,
            entry.agent_name or "",
            entry.applied_topology_version or "",
            " ".join(entry.groups),
            " ".join(entry.topology_issues),
            " ".join(controller.node.services),
        ]
        haystack = " ".join(terms).lower()
        return self.fleet_filter_query.lower() in haystack

    def fleet_groups(self) -> list[tuple[str, list[NodeRegistryEntry]]]:
        grouped: dict[str, list[NodeRegistryEntry]] = {}
        for entry in self.cluster.list_registry_entries():
            if not self.entry_matches_fleet_filters(entry):
                continue
            for key in self.fleet_group_values(entry):
                grouped.setdefault(key, []).append(entry)
        return [(key, sorted(grouped[key], key=lambda item: item.node_name)) for key in sorted(grouped)]

    def current_group_key(self) -> str:
        groups = self.fleet_groups()
        if not groups:
            return "ungrouped"
        visible_keys = {key for key, _entries in groups}
        values = self.fleet_group_values(self.current_registry_entry)
        for value in values:
            if value in visible_keys:
                return value
        return groups[0][0]

    def current_group_entries(self, include_collapsed: bool = False) -> list[NodeRegistryEntry]:
        target = self.current_group_key()
        if not include_collapsed and target in self.fleet_collapsed_groups:
            return []
        for key, entries in self.fleet_groups():
            if key == target:
                return entries
        return []

    def visible_fleet_node_names(self) -> list[str]:
        names: list[str] = []
        for key, entries in self.fleet_groups():
            if key in self.fleet_collapsed_groups:
                continue
            for entry in entries:
                names.append(entry.node_name)
        return names

    def ensure_fleet_selection_visible(self) -> None:
        if not self.cluster.controllers:
            return
        visible = self.visible_fleet_node_names()
        if not visible:
            return
        if self.current_node_name in visible:
            return
        for index, controller in enumerate(self.cluster.controllers):
            if controller.node.name == visible[0]:
                self.selected_node_index = index
                return

    def move_group_selection(self, delta: int) -> None:
        groups = self.fleet_groups()
        if not groups:
            return
        keys = [key for key, _entries in groups]
        current = self.current_group_key()
        current_index = keys.index(current) if current in keys else 0
        next_key = keys[(current_index + delta) % len(keys)]
        for key, entries in groups:
            if key != next_key:
                continue
            if entries:
                self.select_node_by_name(entries[0].node_name)
                self.message = f"Fleet group: {next_key}"
            return

    def move_node_within_group(self, delta: int) -> None:
        entries = self.current_group_entries()
        if not entries:
            return
        node_names = [entry.node_name for entry in entries]
        current_index = 0
        if self.current_node_name in node_names:
            current_index = node_names.index(self.current_node_name)
        next_index = (current_index + delta) % len(node_names)
        self.select_node_by_name(node_names[next_index])
        self.message = f"Fleet node: {node_names[next_index]}"

    def toggle_mark_current_node(self) -> None:
        node_name = self.current_node_name
        if node_name in self.marked_nodes:
            self.marked_nodes.remove(node_name)
            self.message = f"Unmarked {node_name}"
        else:
            self.marked_nodes.add(node_name)
            self.message = f"Marked {node_name}"

    def mark_current_group(self) -> None:
        entries = self.current_group_entries()
        if not entries:
            self.message = "Current group is empty"
            return
        for entry in entries:
            self.marked_nodes.add(entry.node_name)
        self.message = f"Marked group {self.current_group_key()} ({len(entries)} nodes)"

    def toggle_current_group_collapsed(self) -> None:
        group_key = self.current_group_key()
        if group_key in self.fleet_collapsed_groups:
            self.fleet_collapsed_groups.remove(group_key)
            self.message = f"Expanded group {group_key}"
        else:
            self.fleet_collapsed_groups.add(group_key)
            self.ensure_fleet_selection_visible()
            self.message = f"Collapsed group {group_key}"

    def collapse_visible_groups(self) -> None:
        self.fleet_collapsed_groups.update(key for key, _entries in self.fleet_groups())
        self.ensure_fleet_selection_visible()
        self.message = "Collapsed all visible Fleet groups"

    def expand_all_groups(self) -> None:
        self.fleet_collapsed_groups.clear()
        self.message = "Expanded all Fleet groups"

    def _set_batch_results(self, title: str, results: dict[str, dict]) -> None:
        lines = [title]
        failed = []
        for node_name in sorted(results):
            item = results[node_name]
            if item.get("error"):
                failed.append(node_name)
                lines.append(f"{node_name}: ERROR {item['error']}")
            else:
                lines.append(f"{node_name}: ok {item.get('task_id') or item.get('version') or ''}".strip())
        self.batch_result_lines = lines[:8]
        self.message = title if not failed else f"{title} (errors: {', '.join(failed[:3])})"

    def run_batch_action(self, action: str, server_name: Optional[str], label: str) -> None:
        node_names = sorted(self.marked_nodes)
        if not node_names:
            self.message = "No nodes marked for batch action"
            return
        if server_name:
            node_names = [
                node_name
                for node_name in node_names
                if server_name in self.cluster.get_controller(node_name).node.services
            ]
            if not node_names:
                self.message = f"No marked nodes expose service {server_name}"
                return
        results = self.cluster.queue_action_many(node_names, action, server_name=server_name)
        suffix = f" ({server_name})" if server_name else ""
        self._set_batch_results(f"{label} {len(node_names)} nodes{suffix}", results)
        self.refresh(force=True)

    def run_batch_topology(self) -> None:
        node_names = sorted(self.marked_nodes)
        if not node_names:
            self.message = "No nodes marked for topology push"
            return
        results = self.cluster.push_topology_many(node_names)
        self._set_batch_results(f"Pushed topology to {len(node_names)} nodes", results)
        self.refresh(force=True)

    def move_task_selection(self, delta: int) -> None:
        if not self.tasks:
            return
        self.selected_task_index = min(max(0, self.selected_task_index + delta), len(self.tasks) - 1)
        self.task_follow_output = True
        self.task_output_scroll_offset = 0

    def scroll_active_view(self, direction: int) -> None:
        step_logs = max(1, self.last_log_body_height // 2)
        step_tasks = max(1, self.last_task_output_height // 2)

        if self.active_tab == "Logs":
            if direction > 0:
                self.log_follow = False
                self.log_scroll_offset += step_logs
            else:
                self.log_scroll_offset = max(0, self.log_scroll_offset - step_logs)
                if self.log_scroll_offset == 0:
                    self.log_follow = True
            self.message = f"Log offset: {self.log_scroll_offset}"
        elif self.active_tab == "Tasks":
            if direction > 0:
                self.task_follow_output = False
                self.task_output_scroll_offset += step_tasks
            else:
                self.task_output_scroll_offset = max(0, self.task_output_scroll_offset - step_tasks)
                if self.task_output_scroll_offset == 0:
                    self.task_follow_output = True
            self.message = f"Task output offset: {self.task_output_scroll_offset}"

    def jump_active_view_to_oldest(self) -> None:
        if self.active_tab == "Logs":
            self.log_follow = False
            self.log_scroll_offset = MAX_TAIL_READ_LINES
            self.message = "Moved to older log lines"
        elif self.active_tab == "Tasks":
            self.task_follow_output = False
            self.task_output_scroll_offset = 10**6
            self.message = "Moved to older task output"

    def jump_active_view_to_latest(self) -> None:
        if self.active_tab == "Logs":
            self.log_follow = True
            self.log_scroll_offset = 0
            self.message = "Moved to latest log lines"
        elif self.active_tab == "Tasks":
            self.task_follow_output = True
            self.task_output_scroll_offset = 0
            self.message = "Moved to latest task output"

    @property
    def active_tab(self) -> str:
        return TABS[self.active_tab_index]

    @property
    def current_node(self) -> BaseNodeController:
        return self.cluster.controllers[self.selected_node_index]

    @property
    def current_controller(self):
        return self.cluster.controllers[self.selected_node_index]

    @property
    def current_node_name(self) -> str:
        return self.current_controller.node.name

    @property
    def current_registry_entry(self) -> NodeRegistryEntry:
        return self.cluster.registry[self.current_node_name]

    def _short_time(self, value: Optional[str]) -> str:
        if not value:
            return "-"
        if "T" in value:
            return value.split("T", 1)[1][:8]
        return value[:8]

    @property
    def selected_server(self) -> Optional[dict]:
        if not self.services:
            return None
        return self.services[self.selected_service_index]

    @property
    def selected_server_name(self) -> Optional[str]:
        selected = self.selected_server
        return selected["name"] if selected else None

    @property
    def selected_task(self) -> Optional[dict]:
        if not self.tasks:
            return None
        return self.tasks[self.selected_task_index]

    def draw(self, stdscr: curses.window) -> None:
        stdscr.erase()
        height, width = stdscr.getmaxyx()

        if height < MIN_HEIGHT or width < MIN_WIDTH:
            safe_addstr(
                stdscr,
                0,
                0,
                f"Terminal too small: need at least {MIN_WIDTH}x{MIN_HEIGHT}, got {width}x{height}",
                TuiColors.FAILED,
            )
            safe_addstr(stdscr, 2, 0, "Resize the terminal and retry. Press q to quit.", 0)
            stdscr.refresh()
            return

        self.draw_header(stdscr, width)
        body_top = 4
        footer_top = height - 2
        body_height = footer_top - body_top

        body = stdscr.derwin(body_height, width, body_top, 0)

        if self.active_tab == "Overview":
            self.draw_overview(body)
        elif self.active_tab == "Logs":
            self.draw_logs_view(body)
        elif self.active_tab == "Tasks":
            self.draw_tasks_view(body)
        elif self.active_tab == "Fleet":
            self.draw_fleet_view(body)
        else:
            self.draw_help_view(body)

        self.draw_footer(stdscr, height, width)
        stdscr.refresh()

    def draw_header(self, stdscr: curses.window, width: int) -> None:
        safe_addstr(stdscr, 0, 0, clip_text("Mession Server Manager TUI", width), TuiColors.HEADER)

        x = 0
        for index, tab_name in enumerate(TABS):
            label = f" {index + 1}.{tab_name} "
            attr = TuiColors.TAB_ACTIVE if index == self.active_tab_index else curses.A_BOLD
            safe_addstr(stdscr, 1, x, label, attr)
            x += len(label) + 1

        running_count = self.snapshot.get("running_count", 0)
        service_count = self.snapshot.get("service_count", 0)
        updated_at = self.snapshot.get("updated_at", "-")
        node = self.current_node.node
        registry = self.current_registry_entry
        summary = (
            f"Node: {node.name} ({node.mode}:{node.host})   "
            f"Registry: {self.cluster.registry_mode_label()}   "
            f"Build: {self.snapshot.get('build_dir', self.build_dir)}   "
            f"Heartbeat: {registry.heartbeat_state}   "
            f"Running: {running_count}/{service_count}   Marked: {len(self.marked_nodes)}   Filter: {self.fleet_filter_query or '-'}   Updated: {updated_at}"
        )
        safe_addstr(stdscr, 2, 0, clip_text(summary, width), 0)

        selected = self.selected_server
        if selected:
            line = (
                f"Selected: {selected['name']}  state={selected['state']}  port={selected['port']}  "
                f"pid={selected['tracked_pid'] or '-'}  node={node.name}  topo={registry.applied_topology_version or '-'}  tab={self.active_tab}"
            )
        else:
            line = f"Selected: -  node={node.name}  topo={registry.applied_topology_version or '-'}  tab={self.active_tab}"
        safe_addstr(stdscr, 3, 0, clip_text(line, width), TuiColors.MUTED)

    def draw_overview(self, body: curses.window) -> None:
        height, width = body.getmaxyx()
        left_width = max(34, int(width * 0.38))
        right_width = width - left_width
        top_height = max(9, int(height * 0.48))

        services_win = body.derwin(height, left_width, 0, 0)
        details_win = body.derwin(top_height, right_width, 0, left_width)
        registry_win = body.derwin(height - top_height, right_width, top_height, left_width)

        self.draw_services_panel(services_win)
        self.draw_selected_server_panel(details_win)
        self.draw_node_registry_panel(registry_win)

    def draw_logs_view(self, body: curses.window) -> None:
        height, width = body.getmaxyx()
        left_width = max(30, int(width * 0.30))
        right_width = width - left_width

        services_win = body.derwin(height, left_width, 0, 0)
        log_win = body.derwin(height, right_width, 0, left_width)

        self.draw_services_panel(services_win)
        self.draw_live_log_panel(log_win)

    def draw_tasks_view(self, body: curses.window) -> None:
        height, width = body.getmaxyx()
        top_height = max(8, int(height * 0.38))

        task_list_win = body.derwin(top_height, width, 0, 0)
        task_output_win = body.derwin(height - top_height, width, top_height, 0)

        self.draw_task_list_panel(task_list_win)
        self.draw_task_output_panel(task_output_win)

    def draw_fleet_view(self, body: curses.window) -> None:
        height, width = body.getmaxyx()
        left_width = max(26, int(width * 0.28))
        right_width = width - left_width
        top_height = max(9, int(height * 0.48))

        groups_win = body.derwin(height, left_width, 0, 0)
        nodes_win = body.derwin(top_height, right_width, 0, left_width)
        batch_win = body.derwin(height - top_height, right_width, top_height, left_width)

        self.draw_fleet_groups_panel(groups_win)
        self.draw_fleet_nodes_panel(nodes_win)
        self.draw_batch_panel(batch_win)

    def draw_help_view(self, body: curses.window) -> None:
        draw_border(body, "Help")
        lines = [
            "Tab / h / l / 1-5 : switch views",
            ", / . : switch node",
            "Up / Down / j / k : select server",
            "[ / ] : select task",
            "PgUp / PgDn : scroll logs or task output in the active tab",
            "Home / End : jump to oldest/latest lines in Logs or Tasks",
            "f : toggle follow mode for Logs or Tasks",
            "a : start all servers",
            "X : stop all servers",
            "s : start selected server",
            "x : stop selected server",
            "R : restart selected server",
            "b : build",
            "v : validate without build",
            "V : validate with build",
            "p : push topology to current node",
            "P : push topology to all nodes",
            "Fleet: / search | z clear | o marked-only | i issue-only",
            "Fleet: g group mode | { } switch group | c/C/e collapse",
            "Fleet: m mark node | M mark group | u clear marks",
            "Fleet: a/X/s/x/R/b/v/V/p run batch actions on marked nodes",
            "r : refresh snapshot",
            "q : quit",
            "",
            "Overview: service list + selected server summary + node registry",
            "Logs: live log tail with follow and scroll",
            "Tasks: task list + streaming task output",
            "Fleet: registry-driven node groups + batch operations",
        ]
        for row, line in enumerate(lines, start=1):
            if row >= body.getmaxyx()[0] - 1:
                break
            safe_addstr(body, row, 1, line, 0)

    def draw_services_panel(self, window: curses.window) -> None:
        draw_border(window, "Services")
        safe_addstr(window, 1, 1, "Name            State      Port   PID", curses.A_BOLD)

        visible_rows = max(0, window.getmaxyx()[0] - 3)
        for row in range(visible_rows):
            if row >= len(self.services):
                break
            service = self.services[row]
            line = f"{service['name']:<14}  {service['state']:<9}  {service['port']:>5}  {str(service['tracked_pid'] or '-'):>8}"
            attr = self.color_for_state(service["state"])
            if row == self.selected_service_index:
                attr |= TuiColors.HIGHLIGHT
            safe_addstr(window, row + 2, 1, line, attr)

    def draw_selected_server_panel(self, window: curses.window) -> None:
        title = "Server Detail"
        if self.selected_server_name:
            title += f" - {self.selected_server_name}"
        draw_border(window, title)

        selected = self.selected_server
        if not selected:
            safe_addstr(window, 1, 1, "No service selected", TuiColors.MUTED)
            return

        details = [
            f"Node: {self.current_node.node.name} ({self.current_node.node.mode}:{self.current_node.node.host})",
            f"Registry Source: {self.current_registry_entry.registry_source}",
            f"Heartbeat: {self.current_registry_entry.heartbeat_state}",
            f"Manageable: {self.current_registry_entry.manageable}",
            f"Discovered: {self.current_registry_entry.discovered}",
            f"First Seen: {self.current_registry_entry.first_seen_at or '-'}",
            f"Last Seen: {self.current_registry_entry.last_seen_at or '-'}",
            f"Agent Name: {self.current_registry_entry.agent_name or '-'}",
            f"Agent Id: {(self.current_registry_entry.agent_id or '-')[:12]}",
            f"Groups: {', '.join(self.current_registry_entry.groups) or '-'}",
            f"Applied Topology: {self.current_registry_entry.applied_topology_version or '-'}",
            f"State: {selected['state']}",
            f"Port: {selected['port']}",
            f"Tracked PID: {selected['tracked_pid'] or '-'}",
            f"PID Alive: {selected['tracked_pid_alive']}",
            f"Port Open: {selected['port_open']}",
            f"Log Exists: {selected['log_exists']}",
            f"Log Size: {selected['log_size']}",
            f"Log Modified: {selected['log_modified_at'] or '-'}",
            f"Snapshot Error: {self.snapshot.get('error') or '-'}",
            "",
            "Actions: s start selected | x stop selected | R restart selected",
            "Global: a start all | X stop all | b build | v/V validate | p/P topology",
            "Nodes: , previous node | . next node",
        ]
        for row, line in enumerate(details, start=1):
            if row >= window.getmaxyx()[0] - 1:
                break
            safe_addstr(window, row, 1, line, 0)

    def draw_node_registry_panel(self, window: curses.window) -> None:
        draw_border(window, "Node Registry")
        entries = self.cluster.list_registry_entries()
        if not entries:
            safe_addstr(window, 1, 1, "No nodes configured.", TuiColors.MUTED)
            return

        cluster_issues = self.cluster.cluster_issues
        registry_summary = self.cluster.central_registry_summary
        registry_label = self.cluster.registry_mode_label()
        if registry_label == "central":
            registry_text = (
                f"Registry: central online={registry_summary.get('online_count', 0)}/"
                f"{registry_summary.get('node_count', 0)} stale={registry_summary.get('stale_count', 0)}"
            )
        else:
            registry_text = "Registry: controller-side polling"
        safe_addstr(
            window,
            1,
            1,
            clip_text(
                f"Topology version: {self.cluster.config.topology.version}   {registry_text}   cluster_issues={len(cluster_issues)}",
                window.getmaxyx()[1] - 2,
            ),
            curses.A_BOLD,
        )

        visible_rows = max(0, window.getmaxyx()[0] - 5)
        for row in range(min(visible_rows, len(entries))):
            entry = entries[row]
            marker = ">" if entry.node_name == self.current_node_name else " "
            issues_count = len(entry.topology_issues)
            line = (
                f"{marker} {entry.node_name:<14} {entry.heartbeat_state:<8} "
                f"mode={entry.mode:<8} src={entry.registry_source:<10} issues={issues_count:<2} last={self._short_time(entry.last_seen_at)}"
            )
            attr = curses.A_BOLD if entry.node_name == self.current_node_name else 0
            if entry.heartbeat_state == "online":
                attr |= TuiColors.RUNNING
            elif entry.heartbeat_state == "stale":
                attr |= TuiColors.STARTING
            else:
                attr |= TuiColors.FAILED
            safe_addstr(window, row + 2, 1, line, attr)

        info_top = 2 + min(visible_rows, len(entries)) + 1
        current_issues = self.current_registry_entry.topology_issues
        if cluster_issues and info_top < window.getmaxyx()[0] - 1:
            safe_addstr(window, info_top, 1, clip_text(f"Cluster: {cluster_issues[0]}", window.getmaxyx()[1] - 2), TuiColors.MUTED)
            info_top += 1
        for issue in current_issues[:2]:
            if info_top >= window.getmaxyx()[0] - 1:
                break
            safe_addstr(window, info_top, 1, clip_text(f"Node issue: {issue}", window.getmaxyx()[1] - 2), TuiColors.MUTED)
            info_top += 1

    def draw_fleet_groups_panel(self, window: curses.window) -> None:
        draw_border(window, "Fleet Groups")
        groups = self.fleet_groups()
        if not groups:
            safe_addstr(window, 1, 1, "No registry groups yet.", TuiColors.MUTED)
            return

        safe_addstr(
            window,
            1,
            1,
            clip_text(
                f"Mode: {self.fleet_group_mode}   current={self.current_group_key()}   search={self.fleet_filter_query or '-'}",
                window.getmaxyx()[1] - 2,
            ),
            curses.A_BOLD,
        )
        visible_rows = max(0, window.getmaxyx()[0] - 3)
        current_group = self.current_group_key()
        for row in range(min(visible_rows, len(groups))):
            key, entries = groups[row]
            online = sum(1 for entry in entries if entry.heartbeat_state == "online")
            marked = sum(1 for entry in entries if entry.node_name in self.marked_nodes)
            collapsed = "+" if key in self.fleet_collapsed_groups else "-"
            issue_count = sum(1 for entry in entries if entry.topology_issues or entry.last_error)
            line = f"{collapsed} {key:<10} nodes={len(entries):<2} online={online:<2} issues={issue_count:<2} marked={marked:<2}"
            attr = curses.A_BOLD if key == current_group else 0
            if key == current_group:
                attr |= TuiColors.HIGHLIGHT
            safe_addstr(window, row + 2, 1, line, attr)

    def draw_fleet_nodes_panel(self, window: curses.window) -> None:
        draw_border(window, f"Fleet Nodes - {self.current_group_key()}")
        if self.current_group_key() in self.fleet_collapsed_groups:
            safe_addstr(window, 1, 1, "Current group is collapsed. Press c to expand.", TuiColors.MUTED)
            return
        entries = self.current_group_entries()
        if not entries:
            safe_addstr(window, 1, 1, "No nodes in current group.", TuiColors.MUTED)
            return

        safe_addstr(window, 1, 1, "Mark Node           State    Mode      Run  Svc  Topology", curses.A_BOLD)
        visible_rows = max(0, window.getmaxyx()[0] - 3)
        for row in range(min(visible_rows, len(entries))):
            entry = entries[row]
            marker = "*" if entry.node_name in self.marked_nodes else " "
            cursor = ">" if entry.node_name == self.current_node_name else " "
            line = (
                f"{marker}{cursor} {entry.node_name:<14} {entry.heartbeat_state:<8} {entry.mode:<9} "
                f"{entry.running_count:>2}  {entry.service_count:>2}  {entry.applied_topology_version or '-'}"
            )
            attr = curses.A_BOLD if entry.node_name == self.current_node_name else 0
            if entry.heartbeat_state == "online":
                attr |= TuiColors.RUNNING
            elif entry.heartbeat_state == "stale":
                attr |= TuiColors.STARTING
            else:
                attr |= TuiColors.FAILED
            if entry.topology_issues or entry.last_error:
                attr |= curses.A_UNDERLINE
            safe_addstr(window, row + 2, 1, clip_text(line, window.getmaxyx()[1] - 2), attr)

    def draw_batch_panel(self, window: curses.window) -> None:
        draw_border(window, "Batch Operations")
        marked_entries = [
            entry for entry in self.cluster.list_registry_entries()
            if entry.node_name in self.marked_nodes
        ]
        manageable_count = sum(1 for entry in marked_entries if entry.manageable)
        lines = [
            f"Marked: {len(marked_entries)}   manageable={manageable_count}   group_mode={self.fleet_group_mode}",
            f"Search={self.fleet_filter_query or '-'}   marked_only={self.fleet_show_marked_only}   issue_only={self.fleet_show_issue_only}",
            "Keys: / search | z clear | g group | { } switch | c/C/e collapse",
            "Batch: a/X all-services | s/x/R selected-service | b/v/V build/validate | p topology",
        ]
        for row, line in enumerate(lines, start=1):
            safe_addstr(window, row, 1, clip_text(line, window.getmaxyx()[1] - 2), 0)

        row = 5
        if marked_entries:
            for entry in marked_entries[:4]:
                line = (
                    f"{entry.node_name:<14} {entry.heartbeat_state:<8} manageable={str(entry.manageable):<5} "
                    f"groups={','.join(entry.groups) or '-'}"
                )
                safe_addstr(window, row, 1, clip_text(line, window.getmaxyx()[1] - 2), 0)
                row += 1
        else:
            safe_addstr(window, row, 1, "No marked nodes yet.", TuiColors.MUTED)
            row += 1

        if row < window.getmaxyx()[0] - 1:
            safe_addstr(window, row, 1, "Last batch result:", curses.A_BOLD)
            row += 1
        if self.batch_result_lines:
            for line in self.batch_result_lines[: max(0, window.getmaxyx()[0] - row - 1)]:
                safe_addstr(window, row, 1, clip_text(line, window.getmaxyx()[1] - 2), TuiColors.MUTED)
                row += 1
        elif row < window.getmaxyx()[0] - 1:
            safe_addstr(window, row, 1, "(No batch actions yet)", TuiColors.MUTED)

    def draw_live_log_panel(self, window: curses.window) -> None:
        selected = self.selected_server
        title = "Live Logs"
        if selected:
            title += f" - {selected['name']}"
        draw_border(window, title)

        if not selected:
            safe_addstr(window, 1, 1, "No service selected", TuiColors.MUTED)
            return

        body_height = max(1, window.getmaxyx()[0] - 3)
        self.last_log_body_height = body_height

        buffer_lines = max(400, min(MAX_TAIL_READ_LINES, body_height + self.log_scroll_offset + 200))
        raw = self.current_node.read_log(selected["name"], buffer_lines)
        lines = raw.splitlines() if raw else []
        visible_lines, self.log_scroll_offset = slice_visible_lines(
            lines,
            body_height,
            follow=self.log_follow,
            offset=self.log_scroll_offset,
        )

        status = (
            f"node={self.current_node.node.name} "
            f"follow={'on' if self.log_follow else 'off'} offset={self.log_scroll_offset} lines={len(lines)}"
        )
        safe_addstr(window, 1, 1, clip_text(status, window.getmaxyx()[1] - 2), TuiColors.MUTED)

        if not visible_lines:
            safe_addstr(window, 2, 1, "No log content yet.", TuiColors.MUTED)
            return

        for row, line in enumerate(visible_lines, start=2):
            if row >= window.getmaxyx()[0] - 1:
                break
            safe_addstr(window, row, 1, line, 0)

    def draw_task_list_panel(self, window: curses.window) -> None:
        draw_border(window, "Tasks")
        if not self.current_controller.supports_tasks():
            safe_addstr(window, 1, 1, "Tasks unavailable for registry-only nodes.", TuiColors.MUTED)
            return
        if not self.tasks:
            safe_addstr(window, 1, 1, "No tasks yet. Use a/s/x/R/b/v/V to queue work.", TuiColors.MUTED)
            return

        safe_addstr(window, 1, 1, "Name                      Status      RC   Created", curses.A_BOLD)
        visible_rows = max(0, window.getmaxyx()[0] - 3)
        start_index = 0
        if self.selected_task_index >= visible_rows:
            start_index = self.selected_task_index - visible_rows + 1

        for row in range(visible_rows):
            task_index = start_index + row
            if task_index >= len(self.tasks):
                break
            task = self.tasks[task_index]
            created = (task["created_at"] or "-").split("T")[-1][:8]
            line = f"{task['name']:<24}  {task['status']:<10}  {str(task['return_code'] if task['return_code'] is not None else '-'):>2}   {created}"
            attr = curses.A_BOLD if task_index == self.selected_task_index else 0
            if task["status"] == "running":
                attr |= TuiColors.STARTING
            elif task["status"] == "completed":
                attr |= TuiColors.RUNNING
            elif task["status"] == "failed":
                attr |= TuiColors.FAILED
            if task_index == self.selected_task_index:
                attr |= TuiColors.HIGHLIGHT
            safe_addstr(window, row + 2, 1, line, attr)

    def draw_task_output_panel(self, window: curses.window) -> None:
        draw_border(window, "Task Output")
        task = self.selected_task
        if not task:
            safe_addstr(window, 1, 1, "No task selected", TuiColors.MUTED)
            return

        meta_line = (
            f"name={task['name']} status={task['status']} rc={task['return_code'] if task['return_code'] is not None else '-'} "
            f"follow={'on' if self.task_follow_output else 'off'} offset={self.task_output_scroll_offset}"
        )
        safe_addstr(window, 1, 1, clip_text(meta_line, window.getmaxyx()[1] - 2), TuiColors.MUTED)

        command_line = "Command: " + " ".join(task["command"])
        safe_addstr(window, 2, 1, clip_text(command_line, window.getmaxyx()[1] - 2), curses.A_BOLD)

        body_height = max(1, window.getmaxyx()[0] - 4)
        self.last_task_output_height = body_height
        lines = task["output"].splitlines()
        visible_lines, self.task_output_scroll_offset = slice_visible_lines(
            lines,
            body_height,
            follow=self.task_follow_output,
            offset=self.task_output_scroll_offset,
        )

        if not visible_lines:
            safe_addstr(window, 3, 1, "(No output yet)", TuiColors.MUTED)
            return

        for row, line in enumerate(visible_lines, start=3):
            if row >= window.getmaxyx()[0] - 1:
                break
            safe_addstr(window, row, 1, line, 0)

    def draw_footer(self, stdscr: curses.window, height: int, width: int) -> None:
        help_line = (
            "tab switch view | ,/. node | arrows/jk service | [ ] task | PgUp/PgDn scroll | f follow | "
            "a/X all | s/x/R selected | b/v/V build+validate | p/P topology | Fleet: / z o i c batch | q quit"
        )
        safe_addstr(stdscr, height - 2, 0, clip_text(help_line, width), TuiColors.MUTED)
        status_prefix = "Search> " if self.fleet_filter_editing else "Status: "
        status_text = self.fleet_filter_query if self.fleet_filter_editing else self.message
        safe_addstr(stdscr, height - 1, 0, clip_text(f"{status_prefix}{status_text}", width), 0)

    def color_for_state(self, state: str) -> int:
        if state == "Running":
            return TuiColors.RUNNING
        if state == "Starting":
            return TuiColors.STARTING
        return TuiColors.STOPPED


def main() -> int:
    parser = argparse.ArgumentParser(description="Terminal UI for local Mession server management")
    parser.add_argument("--build-dir", type=Path, default=Path("Build"), help="Build directory (default: Build)")
    parser.add_argument("--cluster-config", type=Path, help="Optional cluster config JSON path")
    parser.add_argument(
        "--refresh-interval",
        type=float,
        default=DEFAULT_REFRESH_INTERVAL,
        help=f"UI refresh interval in seconds (default: {DEFAULT_REFRESH_INTERVAL})",
    )
    args = parser.parse_args()

    build_dir = resolve_build_dir(args.build_dir)
    app = ServerManagerTui(
        build_dir=build_dir,
        refresh_interval=args.refresh_interval,
        cluster_config=args.cluster_config,
    )
    return curses.wrapper(app.run)


if __name__ == "__main__":
    raise SystemExit(main())
