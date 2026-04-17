const COLUMN_DEFS = [
  { key: "select", label: "", width: 46, minWidth: 46, sticky: "select", sortField: null },
  { key: "asset", label: "资产", width: 240, minWidth: 180, sticky: "asset", sortField: "asset" },
  { key: "monsterTemplateId", label: "模板", width: 120, minWidth: 96, sortField: "monsterTemplateId" },
  { key: "debugName", label: "调试名", width: 180, minWidth: 140, sortField: "debugName" },
  { key: "sceneId", label: "场景", width: 100, minWidth: 84, sortField: "sceneId" },
  { key: "currentHealth", label: "当前生命", width: 100, minWidth: 84, sortField: "currentHealth" },
  { key: "maxHealth", label: "最大生命", width: 100, minWidth: 84, sortField: "maxHealth" },
  { key: "attackPower", label: "攻击", width: 92, minWidth: 76, sortField: "attackPower" },
  { key: "defensePower", label: "防御", width: 92, minWidth: 76, sortField: "defensePower" },
  { key: "primarySkillId", label: "主技能", width: 130, minWidth: 110, sortField: "primarySkillId" },
  { key: "experienceReward", label: "经验", width: 92, minWidth: 76, sortField: "experienceReward" },
  { key: "goldReward", label: "金币", width: 92, minWidth: 76, sortField: "goldReward" },
  { key: "skills", label: "技能列表", width: 180, minWidth: 140, sortField: "skills" },
  { key: "dirty", label: "修改", width: 92, minWidth: 80, sortField: "dirty" },
  { key: "validation", label: "校验", width: 110, minWidth: 96, sortField: "validation" },
];

const EDITABLE_FIELD_KEYS = [
  "monsterTemplateId",
  "debugName",
  "sceneId",
  "currentHealth",
  "maxHealth",
  "attackPower",
  "defensePower",
  "primarySkillId",
  "experienceReward",
  "goldReward",
];

const NUMERIC_FIELD_KEYS = new Set([
  "monsterTemplateId",
  "sceneId",
  "currentHealth",
  "maxHealth",
  "attackPower",
  "defensePower",
  "primarySkillId",
  "experienceReward",
  "goldReward",
]);

const COLUMN_BY_KEY = Object.fromEntries(COLUMN_DEFS.map((column) => [column.key, column]));
const FIELD_LABELS = {
  asset: "资产",
  monsterTemplateId: "模板",
  debugName: "调试名",
  sceneId: "场景",
  currentHealth: "当前生命",
  maxHealth: "最大生命",
  attackPower: "攻击",
  defensePower: "防御",
  primarySkillId: "主技能",
  experienceReward: "经验",
  goldReward: "金币",
  skills: "技能列表",
  dirty: "修改",
  validation: "校验",
};
const ASSET_TAB_LABELS = {
  monster: "怪物配置",
  skill: "技能配置",
  buff: "增益配置",
  drop: "掉落配置",
};
const MAX_HISTORY_ENTRIES = 80;

const state = {
  rows: [],
  filterText: "",
  assetTab: "monster",
  selectedSourcePath: "",
  selectedSourcePaths: [],
  selectionAnchorSourcePath: "",
  activeCell: null,
  sort: {
    field: "asset",
    direction: "asc",
  },
  columnWidths: Object.fromEntries(COLUMN_DEFS.map((column) => [column.key, column.width])),
  history: [],
  redoStack: [],
};

const ui = {};
let isHydratingInspector = false;
let resizeSession = null;

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

function getFieldLabel(fieldName) {
  return FIELD_LABELS[fieldName] || fieldName;
}

function buildHistorySnapshot() {
  return {
    rows: state.rows.map((row) => cloneRow(row)),
    selectedSourcePath: state.selectedSourcePath,
    selectedSourcePaths: [...state.selectedSourcePaths],
    selectionAnchorSourcePath: state.selectionAnchorSourcePath,
    activeCell: state.activeCell ? { ...state.activeCell } : null,
  };
}

function serializeHistorySnapshot(snapshot) {
  return JSON.stringify({
    rows: snapshot.rows.map((row) => ({
      identity: row.identity,
      originalSourcePath: row.originalSourcePath,
      model: row.model,
      issues: row.issues,
      hasErrors: row.hasErrors,
      dirty: row.dirty,
      loadSucceeded: row.loadSucceeded,
      loadError: row.loadError,
      validationState: row.validationState,
      persisted: row.persisted,
    })),
    selectedSourcePath: snapshot.selectedSourcePath,
    selectedSourcePaths: snapshot.selectedSourcePaths,
    selectionAnchorSourcePath: snapshot.selectionAnchorSourcePath,
    activeCell: snapshot.activeCell,
  });
}

function clearHistory() {
  state.history = [];
  state.redoStack = [];
}

function pushHistorySnapshot() {
  const snapshot = buildHistorySnapshot();
  const signature = serializeHistorySnapshot(snapshot);
  const lastEntry = state.history[state.history.length - 1];
  if (lastEntry && lastEntry.signature === signature) {
    return;
  }

  state.history.push({ snapshot, signature });
  if (state.history.length > MAX_HISTORY_ENTRIES) {
    state.history.shift();
  }
  state.redoStack = [];
}

function restoreHistorySnapshot(entry) {
  state.rows = entry.snapshot.rows.map((row) => cloneRow(row));
  state.selectedSourcePath = entry.snapshot.selectedSourcePath;
  state.selectedSourcePaths = [...entry.snapshot.selectedSourcePaths];
  state.selectionAnchorSourcePath = entry.snapshot.selectionAnchorSourcePath;
  state.activeCell = entry.snapshot.activeCell ? { ...entry.snapshot.activeCell } : null;
  renderAll();
}

function $(id) {
  return document.getElementById(id);
}

function readUInt(value) {
  const parsed = Number.parseInt(String(value ?? "0"), 10);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : 0;
}

function normalizeCategoryPath(value) {
  return String(value || "")
    .replaceAll("\\", "/")
    .split("/")
    .filter(Boolean)
    .join("/");
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function cssEscape(value) {
  if (window.CSS && typeof window.CSS.escape === "function") {
    return window.CSS.escape(String(value));
  }
  return String(value).replaceAll("\\", "\\\\").replaceAll('"', '\\"');
}

function compareStrings(left, right) {
  return String(left || "").localeCompare(String(right || ""), undefined, { sensitivity: "base", numeric: true });
}

function compareNumbers(left, right) {
  return Number(left || 0) - Number(right || 0);
}

function createDefaultRow(categoryPath = "Combat/Monsters") {
  return {
    identity: {
      assetName: "",
      categoryPath,
      sourcePath: "",
    },
    originalSourcePath: "",
    paths: {
      sourcePath: "",
      exportJsonPath: "",
      exportMobPath: "",
      exportRoundTripPath: "",
      publishMobPath: "",
    },
    model: {
      monsterTemplateId: 0,
      debugName: "",
      spawnParams: {
        sceneId: 0,
        currentHealth: 0,
        maxHealth: 0,
        attackPower: 0,
        defensePower: 0,
        primarySkillId: 1001,
        experienceReward: 0,
        goldReward: 0,
      },
      skillIds: [],
    },
    issues: [],
    hasErrors: false,
    dirty: true,
    loadSucceeded: true,
    loadError: "",
    validationState: "stale",
    persisted: false,
  };
}

function buildPathsFromIdentity(identity) {
  const assetName = identity.assetName || "<asset>";
  const categoryPath = normalizeCategoryPath(identity.categoryPath || "Combat/Monsters");
  const prefix = categoryPath ? `${categoryPath}/` : "";
  return {
    sourcePath: `EditorAssets/${prefix}${assetName}.masset.json`,
    exportJsonPath: `Build/Generated/Assets/${prefix}${assetName}.json`,
    exportMobPath: `Build/Generated/Assets/${prefix}${assetName}.mob`,
    exportRoundTripPath: `Build/Generated/Assets/${prefix}${assetName}.roundtrip.json`,
    publishMobPath: `GameData/${prefix}${assetName}.mob`,
  };
}

function updateRowDerivedState(row) {
  row.identity.assetName = String(row.identity.assetName || "").trim();
  row.identity.categoryPath = normalizeCategoryPath(row.identity.categoryPath);
  row.paths = buildPathsFromIdentity(row.identity);
  row.identity.sourcePath = row.paths.sourcePath;
  row.model.spawnParams.MonsterTemplateId = row.model.monsterTemplateId;
  row.model.spawnParams.DebugName = row.model.debugName;
}

function markRowDirty(row) {
  row.dirty = true;
  row.validationState = "stale";
}

function normalizeRow(inputRow) {
  const row = createDefaultRow();
  row.identity = { ...row.identity, ...(inputRow.identity || {}) };
  row.paths = { ...row.paths, ...(inputRow.paths || {}) };
  row.model = {
    ...row.model,
    ...(inputRow.model || {}),
    spawnParams: {
      ...row.model.spawnParams,
      ...((inputRow.model && inputRow.model.spawnParams) || {}),
    },
    skillIds: [...(((inputRow.model && inputRow.model.skillIds) || []))],
  };
  row.issues = [...((inputRow.issues || []))];
  row.hasErrors = Boolean(inputRow.hasErrors);
  row.dirty = Boolean(inputRow.dirty);
  row.loadSucceeded = inputRow.loadSucceeded !== false;
  row.loadError = inputRow.loadError || "";
  row.persisted = Boolean(inputRow.persisted);

  updateRowDerivedState(row);
  row.originalSourcePath = inputRow.originalSourcePath || (row.persisted ? row.identity.sourcePath : "");
  row.validationState = row.loadSucceeded
    ? (row.issues.length > 0 ? (row.hasErrors ? "error" : "warn") : "ok")
    : "error";
  return row;
}

function cloneRow(row) {
  const cloned = normalizeRow({
    identity: clone(row.identity),
    originalSourcePath: row.originalSourcePath,
    paths: clone(row.paths),
    model: clone(row.model),
    issues: clone(row.issues),
    hasErrors: row.hasErrors,
    dirty: row.dirty,
    loadSucceeded: row.loadSucceeded,
    loadError: row.loadError,
    persisted: row.persisted,
  });
  cloned.validationState = row.validationState;
  return cloned;
}

function findRow(sourcePath) {
  return state.rows.find((row) => row.identity.sourcePath === sourcePath) || null;
}

function getVisibleRows() {
  const filtered = state.rows.filter((row) => {
    if (!state.filterText) {
      return true;
    }

    const haystack = [
      row.identity.assetName,
      row.identity.categoryPath,
      row.identity.sourcePath,
      row.model.debugName,
      row.model.monsterTemplateId,
      row.model.spawnParams.sceneId,
      row.model.skillIds.join(","),
    ].join(" ").toLowerCase();
    return haystack.includes(state.filterText);
  });

  const rows = [...filtered];
  rows.sort((left, right) => compareRows(left, right, state.sort.field, state.sort.direction));
  return rows;
}

function compareRows(left, right, field, direction) {
  const multiplier = direction === "desc" ? -1 : 1;
  let result = 0;

  switch (field) {
  case "asset":
    result = compareStrings(`${left.identity.assetName}|${left.identity.categoryPath}`, `${right.identity.assetName}|${right.identity.categoryPath}`);
    break;
  case "monsterTemplateId":
  case "sceneId":
  case "currentHealth":
  case "maxHealth":
  case "attackPower":
  case "defensePower":
  case "primarySkillId":
  case "experienceReward":
  case "goldReward":
    result = compareNumbers(getFieldValue(left, field), getFieldValue(right, field));
    break;
  case "debugName":
    result = compareStrings(left.model.debugName, right.model.debugName);
    break;
  case "skills":
    result = compareNumbers(left.model.skillIds.length, right.model.skillIds.length) || compareStrings(left.model.skillIds.join(","), right.model.skillIds.join(","));
    break;
  case "dirty":
    result = compareNumbers(left.dirty ? 1 : 0, right.dirty ? 1 : 0);
    break;
  case "validation":
    result = compareNumbers(getValidationRank(left), getValidationRank(right));
    break;
  default:
    result = compareStrings(left.identity.sourcePath, right.identity.sourcePath);
    break;
  }

  if (result === 0) {
    result = compareStrings(left.identity.sourcePath, right.identity.sourcePath);
  }
  return result * multiplier;
}

function getValidationRank(row) {
  if (!row.loadSucceeded) {
    return 4;
  }
  if (row.hasErrors) {
    return 3;
  }
  if (row.validationState === "stale") {
    return 2;
  }
  if (row.issues.length > 0) {
    return 1;
  }
  return 0;
}

function getSelectedRow() {
  return state.selectedSourcePath ? findRow(state.selectedSourcePath) : null;
}

function getSelectedRows() {
  return state.selectedSourcePaths
    .map((sourcePath) => findRow(sourcePath))
    .filter(Boolean);
}

function getSelectedRowsInVisibleOrder() {
  const selectedSet = new Set(state.selectedSourcePaths);
  return getVisibleRows().filter((row) => selectedSet.has(row.identity.sourcePath));
}

function getVisibleSourcePaths() {
  return getVisibleRows().map((row) => row.identity.sourcePath);
}

function isRowSelected(sourcePath) {
  return state.selectedSourcePaths.includes(sourcePath);
}

function countDirtyRows() {
  return state.rows.filter((row) => row.dirty).length;
}

function sanitizeSelection() {
  const existing = new Set(state.rows.map((row) => row.identity.sourcePath));
  state.selectedSourcePaths = state.selectedSourcePaths.filter((sourcePath) => existing.has(sourcePath));
  if (!state.selectedSourcePaths.length) {
    state.selectedSourcePath = "";
    state.selectionAnchorSourcePath = "";
    return;
  }

  if (!existing.has(state.selectedSourcePath)) {
    state.selectedSourcePath = state.selectedSourcePaths[0];
  }
  if (!existing.has(state.selectionAnchorSourcePath)) {
    state.selectionAnchorSourcePath = state.selectedSourcePaths[0];
  }
}

function renderSummary() {
  const selectedCount = state.selectedSourcePaths.length;
  const selectedRow = getSelectedRow();

  if (selectedCount === 0) {
    ui.selectedAssetLabel.textContent = "未选择任何行";
  } else if (selectedCount === 1) {
    ui.selectedAssetLabel.textContent = selectedRow
      ? (selectedRow.identity.assetName || "未命名怪物")
      : "未选择任何行";
  } else {
    ui.selectedAssetLabel.textContent = `已选择 ${selectedCount} 行`;
  }

  ui.selectedCountLabel.textContent = String(selectedCount);
  ui.dirtyCountLabel.textContent = String(countDirtyRows());

  const visibleSourcePaths = getVisibleSourcePaths();
  const visibleSelectedCount = visibleSourcePaths.filter((sourcePath) => isRowSelected(sourcePath)).length;
  ui.selectAllCheckbox.checked = visibleSourcePaths.length > 0 && visibleSelectedCount === visibleSourcePaths.length;
  ui.selectAllCheckbox.indeterminate = visibleSelectedCount > 0 && visibleSelectedCount < visibleSourcePaths.length;
}

function renderAssetTabs() {
  ui.assetTabs.forEach((button) => {
    button.classList.toggle("active", button.dataset.assetTab === state.assetTab);
  });
}

function renderActionButtons() {
  const hasUndo = state.history.length > 0;
  const hasRedo = state.redoStack.length > 0;
  const hasSelection = state.selectedSourcePaths.length > 0;
  const activeEditableField = getActiveEditableField();

  ui.undoAction.disabled = !hasUndo;
  ui.redoAction.disabled = !hasRedo;
  ui.clearSelectedCells.disabled = !hasSelection || !activeEditableField;
}

function renderTableChrome() {
  ui.tableColgroup.innerHTML = COLUMN_DEFS.map((column) => {
    const width = state.columnWidths[column.key] || column.width;
    return `<col style="width:${width}px">`;
  }).join("");

  const selectWidth = state.columnWidths.select || COLUMN_BY_KEY.select.width;
  const assetWidth = state.columnWidths.asset || COLUMN_BY_KEY.asset.width;
  ui.configTable.style.setProperty("--select-col-width", `${selectWidth}px`);
  ui.configTable.style.setProperty("--asset-col-width", `${assetWidth}px`);

  ui.configTable.querySelectorAll("[data-sort-field]").forEach((button) => {
    if (!button.dataset.baseLabel) {
      button.dataset.baseLabel = button.textContent.trim();
    }

    const field = button.dataset.sortField;
    const isActive = state.sort.field === field;
    const arrow = isActive ? (state.sort.direction === "asc" ? " \u2191" : " \u2193") : "";
    button.textContent = `${button.dataset.baseLabel}${arrow}`;
    button.classList.toggle("active", isActive);
    button.setAttribute("aria-pressed", isActive ? "true" : "false");
  });
}

function validationBadge(row) {
  if (!row.loadSucceeded) {
    return '<span class="validation-pill error">加载失败</span>';
  }
  if (row.validationState === "stale") {
    return '<span class="validation-pill stale">待校验</span>';
  }
  if (row.hasErrors) {
    return `<span class="validation-pill error">${row.issues.length} 项</span>`;
  }
  if (row.issues.length > 0) {
    return `<span class="validation-pill warn">${row.issues.length} 项</span>`;
  }
  return '<span class="validation-pill ok">通过</span>';
}

function renderTable() {
  const rows = getVisibleRows();
  ui.tableBody.innerHTML = rows.map((row) => {
    const selectedClass = row.identity.sourcePath === state.selectedSourcePath ? " selected-primary" : "";
    const multiSelectedClass = isRowSelected(row.identity.sourcePath) ? " selected" : "";
    const loadErrorClass = row.loadSucceeded ? "" : " load-error";
    return `
      <tr class="config-row${multiSelectedClass}${selectedClass}${loadErrorClass}" data-source-path="${escapeHtml(row.identity.sourcePath)}">
        <td class="row-select-cell sticky-col sticky-col-select">
          <input class="row-selector" type="checkbox" data-select-checkbox="${escapeHtml(row.identity.sourcePath)}" ${isRowSelected(row.identity.sourcePath) ? "checked" : ""} aria-label="选择 ${escapeHtml(row.identity.assetName || row.identity.sourcePath)}">
        </td>
        <td class="sticky-col sticky-col-asset">
          <button class="row-select-button" type="button" data-select-row="${escapeHtml(row.identity.sourcePath)}">
            <span class="row-primary">${escapeHtml(row.identity.assetName || "未命名")}</span>
            <span class="row-secondary">${escapeHtml(row.identity.categoryPath || "Combat/Monsters")}</span>
          </button>
        </td>
        ${EDITABLE_FIELD_KEYS.map((fieldName) => renderCellInput(row, fieldName)).join("")}
        <td><span class="skills-summary">${escapeHtml(row.model.skillIds.join(", "))}</span></td>
        <td>${row.dirty ? '<span class="dirty-pill dirty">已修改</span>' : '<span class="dirty-pill clean">已保存</span>'}</td>
        <td>${validationBadge(row)}</td>
      </tr>
    `;
  }).join("");

  restoreActiveCellFocus();
}

function renderCellInput(row, fieldName) {
  const fieldValue = getFieldValue(row, fieldName);
  const inputType = NUMERIC_FIELD_KEYS.has(fieldName) ? "number" : "text";
  const extraAttrs = NUMERIC_FIELD_KEYS.has(fieldName)
    ? ' min="0" step="1" inputmode="numeric"'
    : "";
  const activeClass = state.activeCell && state.activeCell.sourcePath === row.identity.sourcePath && state.activeCell.field === fieldName
    ? " cell-active"
    : "";

  return `<td><input class="table-input${activeClass}" type="${inputType}"${extraAttrs} data-field="${fieldName}" data-source-path="${escapeHtml(row.identity.sourcePath)}" value="${escapeHtml(fieldValue)}"></td>`;
}

function getFieldValue(row, fieldName) {
  switch (fieldName) {
  case "monsterTemplateId": return row.model.monsterTemplateId;
  case "debugName": return row.model.debugName;
  case "sceneId": return row.model.spawnParams.sceneId;
  case "currentHealth": return row.model.spawnParams.currentHealth;
  case "maxHealth": return row.model.spawnParams.maxHealth;
  case "attackPower": return row.model.spawnParams.attackPower;
  case "defensePower": return row.model.spawnParams.defensePower;
  case "primarySkillId": return row.model.spawnParams.primarySkillId;
  case "experienceReward": return row.model.spawnParams.experienceReward;
  case "goldReward": return row.model.spawnParams.goldReward;
  default: return "";
  }
}

function setStatus(message, tone = "info") {
  ui.statusCard.className = `status-card ${tone}`;
  ui.statusCard.textContent = message;
}

function setConnectionState(isConnected) {
  ui.connectionBadge.className = `status-pill ${isConnected ? "ok" : "warn"}`;
  ui.connectionBadge.textContent = isConnected ? "在线" : "离线";
}

function fillInspector(row) {
  isHydratingInspector = true;
  ui.inspectorTitle.textContent = row.identity.assetName || "未命名怪物";
  ui.fields.assetName.value = row.identity.assetName || "";
  ui.fields.categoryPath.value = row.identity.categoryPath || "";
  ui.fields.sourcePath.value = row.identity.sourcePath || "";
  ui.fields.monsterTemplateId.value = row.model.monsterTemplateId || 0;
  ui.fields.debugName.value = row.model.debugName || "";
  ui.fields.sceneId.value = row.model.spawnParams.sceneId || 0;
  ui.fields.primarySkillId.value = row.model.spawnParams.primarySkillId || 0;
  ui.fields.currentHealth.value = row.model.spawnParams.currentHealth || 0;
  ui.fields.maxHealth.value = row.model.spawnParams.maxHealth || 0;
  ui.fields.attackPower.value = row.model.spawnParams.attackPower || 0;
  ui.fields.defensePower.value = row.model.spawnParams.defensePower || 0;
  ui.fields.experienceReward.value = row.model.spawnParams.experienceReward || 0;
  ui.fields.goldReward.value = row.model.spawnParams.goldReward || 0;
  ui.skillIdInput.value = "";
  ui.generatedJsonPath.textContent = row.paths.exportJsonPath;
  ui.generatedMobPath.textContent = row.paths.exportMobPath;
  ui.generatedRoundtripPath.textContent = row.paths.exportRoundTripPath;
  ui.publishMobPath.textContent = row.paths.publishMobPath;
  isHydratingInspector = false;

  renderSkillList(row);
  renderIssues(row.issues, row.loadError);
}

function clearInspector() {
  isHydratingInspector = true;
  ui.inspectorTitle.textContent = "未选择任何行";
  Object.values(ui.fields).forEach((field) => {
    field.value = "";
  });
  ui.generatedJsonPath.textContent = "Build/Generated/Assets/...";
  ui.generatedMobPath.textContent = "Build/Generated/Assets/...";
  ui.generatedRoundtripPath.textContent = "Build/Generated/Assets/...";
  ui.publishMobPath.textContent = "GameData/...";
  isHydratingInspector = false;
  ui.skillIdList.innerHTML = '<div class="skill-chip-empty">选择一行后编辑技能列表。</div>';
  ui.issueList.className = "issue-list empty";
  ui.issueList.textContent = "选择一行后查看校验结果。";
}

function renderSkillList(row) {
  if (!row) {
    ui.skillIdList.innerHTML = '<div class="skill-chip-empty">选择一行后编辑技能列表。</div>';
    return;
  }

  if (row.model.skillIds.length === 0) {
    ui.skillIdList.innerHTML = '<div class="skill-chip-empty">当前没有技能，可在上方输入后添加。</div>';
    return;
  }

  ui.skillIdList.innerHTML = row.model.skillIds.map((skillId, index) => `
    <div class="skill-chip">
      <span>${escapeHtml(skillId)}</span>
      <button type="button" data-remove-skill="${index}">x</button>
    </div>
  `).join("");
}

function renderIssues(issues, loadError = "") {
  if (loadError) {
    ui.issueList.className = "issue-list";
    ui.issueList.innerHTML = `
      <article class="issue-item error">
        <strong>加载失败</strong>
        <div>${escapeHtml(loadError)}</div>
      </article>
    `;
    return;
  }

  if (!issues || issues.length === 0) {
    ui.issueList.className = "issue-list empty";
    ui.issueList.textContent = "当前没有问题。";
    return;
  }

  ui.issueList.className = "issue-list";
  ui.issueList.innerHTML = issues.map((issue) => `
    <article class="issue-item ${escapeHtml(issue.severity || "warning")}">
      <strong>${escapeHtml(issue.code || issue.severity || "问题")}</strong>
      <div>${escapeHtml(issue.message || "")}</div>
      <span class="issue-path">${escapeHtml(issue.fieldPath || "")}</span>
    </article>
  `).join("");
}

function renderInspector() {
  const row = getSelectedRow();
  if (!row) {
    clearInspector();
    return;
  }
  fillInspector(row);
}

function renderResult(result) {
  if (!result) {
    ui.resultCard.textContent = "";
    return;
  }

  const lines = [];
  if (result.jsonPath) {
    lines.push(`JSON: ${result.jsonPath}`);
  }
  if (result.mobPath) {
    lines.push(`MOB: ${result.mobPath}`);
  }
  if (result.roundTripPath) {
    lines.push(`回读: ${result.roundTripPath}`);
  }
  if (result.publishPath) {
    lines.push(`发布: ${result.publishPath}`);
  }
  if (Array.isArray(result.results)) {
    result.results.forEach((item) => {
      const prefix = item.previousSourcePath ? `${item.previousSourcePath} -> ${item.sourcePath}` : item.sourcePath;
      lines.push(`${item.ok ? "成功" : "失败"}: ${prefix}${item.error ? ` (${item.error})` : ""}`);
    });
  }
  ui.resultCard.textContent = lines.join("\n");
}

function renderAll() {
  sanitizeSelection();
  renderAssetTabs();
  renderTableChrome();
  renderSummary();
  renderTable();
  renderInspector();
  renderActionButtons();
}

function undoLastAction() {
  if (state.history.length === 0) {
    setStatus("没有可撤销的本地修改。", "info");
    return;
  }

  const currentSnapshot = buildHistorySnapshot();
  const currentEntry = {
    snapshot: currentSnapshot,
    signature: serializeHistorySnapshot(currentSnapshot),
  };
  const previousEntry = state.history.pop();
  state.redoStack.push(currentEntry);
  restoreHistorySnapshot(previousEntry);
  setStatus("已撤销上一条本地修改。", "info");
}

function redoLastAction() {
  if (state.redoStack.length === 0) {
    setStatus("没有可重做的本地修改。", "info");
    return;
  }

  const currentSnapshot = buildHistorySnapshot();
  const currentEntry = {
    snapshot: currentSnapshot,
    signature: serializeHistorySnapshot(currentSnapshot),
  };
  const nextEntry = state.redoStack.pop();
  state.history.push(currentEntry);
  restoreHistorySnapshot(nextEntry);
  setStatus("已重做上一条本地修改。", "info");
}

function clearSelectedCells() {
  const fieldName = getActiveEditableField();
  if (!fieldName) {
    setStatus("请先选中一个可编辑单元格。", "error");
    return;
  }

  const selectedRows = getSelectedRows();
  if (selectedRows.length === 0) {
    setStatus("请先选择至少一行。", "error");
    return;
  }

  pushHistorySnapshot();
  const clearedValue = NUMERIC_FIELD_KEYS.has(fieldName) ? "0" : "";
  applyFieldValueToRows(selectedRows, fieldName, clearedValue);
  renderAll();
  setStatus(`已清空 ${selectedRows.length} 行的${getFieldLabel(fieldName)}列。`, "success");
}

function handleAssetTabClick(tabName) {
  if (tabName === "monster") {
    state.assetTab = "monster";
    renderAll();
    setStatus("当前正在编辑怪物配置。", "info");
    return;
  }

  setStatus(`${ASSET_TAB_LABELS[tabName] || tabName} 仍处于骨架阶段，当前只接入怪物配置。`, "info");
  state.assetTab = "monster";
  renderAll();
}

function setSelection(sourcePaths, primarySourcePath = "") {
  const deduped = [];
  const seen = new Set();
  sourcePaths.forEach((sourcePath) => {
    if (sourcePath && !seen.has(sourcePath) && findRow(sourcePath)) {
      deduped.push(sourcePath);
      seen.add(sourcePath);
    }
  });

  state.selectedSourcePaths = deduped;
  state.selectedSourcePath = primarySourcePath && seen.has(primarySourcePath)
    ? primarySourcePath
    : (deduped[0] || "");
  state.selectionAnchorSourcePath = state.selectedSourcePath;
  renderAll();
}

function selectRow(sourcePath, options = {}) {
  const { additive = false, range = false } = options;
  const visibleSourcePaths = getVisibleSourcePaths();
  if (!sourcePath || !findRow(sourcePath)) {
    return;
  }

  if (range && state.selectionAnchorSourcePath && visibleSourcePaths.length > 0) {
    const anchorIndex = visibleSourcePaths.indexOf(state.selectionAnchorSourcePath);
    const targetIndex = visibleSourcePaths.indexOf(sourcePath);
    if (anchorIndex >= 0 && targetIndex >= 0) {
      const start = Math.min(anchorIndex, targetIndex);
      const end = Math.max(anchorIndex, targetIndex);
      const rangeSelection = visibleSourcePaths.slice(start, end + 1);
      state.selectedSourcePaths = additive
        ? Array.from(new Set([...state.selectedSourcePaths, ...rangeSelection]))
        : rangeSelection;
      state.selectedSourcePath = sourcePath;
      renderAll();
      return;
    }
  }

  if (additive) {
    if (isRowSelected(sourcePath)) {
      state.selectedSourcePaths = state.selectedSourcePaths.filter((entry) => entry !== sourcePath);
      state.selectedSourcePath = state.selectedSourcePaths[0] || "";
    } else {
      state.selectedSourcePaths = [...state.selectedSourcePaths, sourcePath];
      state.selectedSourcePath = sourcePath;
      state.selectionAnchorSourcePath = sourcePath;
    }
    renderAll();
    return;
  }

  state.selectedSourcePaths = [sourcePath];
  state.selectedSourcePath = sourcePath;
  state.selectionAnchorSourcePath = sourcePath;
  renderAll();
}

function setActiveCell(sourcePath, fieldName) {
  state.activeCell = { sourcePath, field: fieldName };
}

function findCellInput(sourcePath, fieldName) {
  return ui.tableBody.querySelector(`.table-input[data-source-path="${cssEscape(sourcePath)}"][data-field="${cssEscape(fieldName)}"]`);
}

function restoreActiveCellFocus() {
  if (!state.activeCell) {
    return;
  }

  const input = findCellInput(state.activeCell.sourcePath, state.activeCell.field);
  if (!input) {
    return;
  }

  if (document.activeElement && document.activeElement !== document.body) {
    return;
  }

  input.focus({ preventScroll: true });
}

function moveActiveCell(rowDelta, fieldDelta) {
  if (!state.activeCell) {
    return;
  }

  const visibleRows = getVisibleRows();
  const rowIndex = visibleRows.findIndex((row) => row.identity.sourcePath === state.activeCell.sourcePath);
  const fieldIndex = EDITABLE_FIELD_KEYS.indexOf(state.activeCell.field);
  if (rowIndex < 0 || fieldIndex < 0) {
    return;
  }

  const nextRowIndex = Math.max(0, Math.min(visibleRows.length - 1, rowIndex + rowDelta));
  const nextFieldIndex = Math.max(0, Math.min(EDITABLE_FIELD_KEYS.length - 1, fieldIndex + fieldDelta));
  const nextRow = visibleRows[nextRowIndex];
  const nextField = EDITABLE_FIELD_KEYS[nextFieldIndex];
  if (!findCellInput(nextRow.identity.sourcePath, nextField)) {
    return;
  }

  setActiveCell(nextRow.identity.sourcePath, nextField);
  if (!isRowSelected(nextRow.identity.sourcePath)) {
    state.selectedSourcePaths = [nextRow.identity.sourcePath];
    state.selectedSourcePath = nextRow.identity.sourcePath;
    state.selectionAnchorSourcePath = nextRow.identity.sourcePath;
    renderAll();
  }

  const focusedInput = findCellInput(nextRow.identity.sourcePath, nextField);
  if (!focusedInput) {
    return;
  }
  focusedInput.focus();
  focusedInput.select();
}

function updateTableField(row, fieldName, rawValue) {
  const value = NUMERIC_FIELD_KEYS.has(fieldName) ? readUInt(rawValue) : String(rawValue ?? "");
  switch (fieldName) {
  case "monsterTemplateId":
    row.model.monsterTemplateId = value;
    break;
  case "debugName":
    row.model.debugName = value.trim();
    break;
  case "sceneId":
    row.model.spawnParams.sceneId = value;
    break;
  case "currentHealth":
    row.model.spawnParams.currentHealth = value;
    break;
  case "maxHealth":
    row.model.spawnParams.maxHealth = value;
    break;
  case "attackPower":
    row.model.spawnParams.attackPower = value;
    break;
  case "defensePower":
    row.model.spawnParams.defensePower = value;
    break;
  case "primarySkillId":
    row.model.spawnParams.primarySkillId = value;
    break;
  case "experienceReward":
    row.model.spawnParams.experienceReward = value;
    break;
  case "goldReward":
    row.model.spawnParams.goldReward = value;
    break;
  default:
    return;
  }

  updateRowDerivedState(row);
  markRowDirty(row);
}

function applyFieldValueToRows(rows, fieldName, rawValue) {
  rows.forEach((row) => updateTableField(row, fieldName, rawValue));
}

function buildDocumentPayload(row) {
  return {
    identity: clone(row.identity),
    model: clone(row.model),
    previousSourcePath: row.originalSourcePath || "",
  };
}

function getDeleteSourcePath(row) {
  return row.originalSourcePath || row.identity.sourcePath;
}

async function fetchJson(url, options = {}) {
  const request = { ...options };
  request.headers = { ...(options.headers || {}) };
  if (request.body && !request.headers["Content-Type"]) {
    request.headers["Content-Type"] = "application/json";
  }

  const response = await fetch(url, request);
  const text = await response.text();
  let payload = {};
  if (text) {
    payload = JSON.parse(text);
  }

  if (!response.ok || payload.ok === false) {
    throw new Error(payload.message || payload.error || `HTTP ${response.status}`);
  }

  return payload;
}

async function loadTable(preferredSelection = "") {
  const payload = await fetchJson("/api/monster-configs/table");
  state.rows = (payload.rows || []).map((row) => normalizeRow({ ...row, persisted: true, originalSourcePath: row.identity?.sourcePath || "" }));
  clearHistory();

  const preferredSelectedPaths = state.selectedSourcePaths.filter((sourcePath) => findRow(sourcePath));
  state.selectedSourcePaths = preferredSelectedPaths;

  const nextSelection = preferredSelection || state.selectedSourcePath;
  if (nextSelection && findRow(nextSelection)) {
    if (!state.selectedSourcePaths.includes(nextSelection)) {
      state.selectedSourcePaths = [nextSelection];
    }
    state.selectedSourcePath = nextSelection;
    state.selectionAnchorSourcePath = nextSelection;
  } else if (state.selectedSourcePaths.length > 0) {
    state.selectedSourcePath = state.selectedSourcePaths[0];
    state.selectionAnchorSourcePath = state.selectedSourcePath;
  } else if (state.rows.length > 0) {
    state.selectedSourcePaths = [state.rows[0].identity.sourcePath];
    state.selectedSourcePath = state.rows[0].identity.sourcePath;
    state.selectionAnchorSourcePath = state.selectedSourcePath;
  } else {
    state.selectedSourcePaths = [];
    state.selectedSourcePath = "";
    state.selectionAnchorSourcePath = "";
  }

  renderAll();
}

async function saveAllDirtyRows() {
  const dirtyRows = state.rows.filter((row) => row.dirty);
  if (dirtyRows.length === 0) {
    setStatus("当前没有需要保存的修改行。", "info");
    renderResult(null);
    return;
  }

  setStatus(`正在保存 ${dirtyRows.length} 行修改...`, "info");
  const payload = await fetchJson("/api/monster-configs/batch-save", {
    method: "POST",
    body: JSON.stringify({ documents: dirtyRows.map(buildDocumentPayload) }),
  });

  renderResult(payload);
  if (payload.hasFailures) {
    setStatus("批量保存完成，但存在失败项。", "error");
    return;
  }

  await loadTable(state.selectedSourcePath);
  setStatus("所有修改行已保存。", "success");
}

async function validateSelectedRow() {
  const row = getSelectedRow();
  if (!row) {
    setStatus("请先选择一行。", "error");
    return;
  }

  setStatus(`正在校验 ${row.identity.assetName || row.identity.sourcePath}...`, "info");
  const payload = await fetchJson("/api/monster-config/validate", {
    method: "POST",
    body: JSON.stringify(buildDocumentPayload(row)),
  });

  row.issues = payload.issues || [];
  row.hasErrors = Boolean(payload.hasErrors);
  row.validationState = row.hasErrors ? "error" : (row.issues.length > 0 ? "warn" : "ok");
  renderAll();
  setStatus(payload.hasErrors ? "校验完成，存在错误。" : "校验通过。", payload.hasErrors ? "error" : "success");
}

async function exportSelectedRow(publishOnly) {
  const row = getSelectedRow();
  if (!row) {
    setStatus("请先选择一行。", "error");
    return;
  }

  const actionLabel = publishOnly ? "正在发布当前行..." : "正在导出当前行...";
  setStatus(actionLabel, "info");
  const payload = await fetchJson("/api/monster-config/export", {
    method: "POST",
    body: JSON.stringify({
      ...buildDocumentPayload(row),
      exportJson: true,
      exportMob: true,
      exportRoundTripJson: false,
      publishMob: publishOnly,
    }),
  });

  renderResult(payload.result);
  renderIssues(payload.result.issues || []);
  setStatus(publishOnly ? "发布完成。" : "导出完成。", "success");
}

function addNewRow() {
  const defaultCategoryPath = state.rows[0]?.identity?.categoryPath || "Combat/Monsters";
  pushHistorySnapshot();
  const row = createDefaultRow(defaultCategoryPath);
  updateRowDerivedState(row);
  state.rows.unshift(row);
  setSelection([row.identity.sourcePath], row.identity.sourcePath);
  setStatus("已新增空白行，保存前请先填写资产名与分类路径。", "info");
}

function buildUniqueIdentity(baseIdentity, usedSourcePaths = new Set()) {
  const categoryPath = normalizeCategoryPath(baseIdentity.categoryPath || "Combat/Monsters");
  const seedName = String(baseIdentity.assetName || "Monster").trim() || "Monster";
  let assetName = seedName;
  let suffix = 1;

  while (true) {
    const candidateIdentity = {
      assetName,
      categoryPath,
      sourcePath: "",
    };
    const candidateSourcePath = buildPathsFromIdentity(candidateIdentity).sourcePath;
    if (!usedSourcePaths.has(candidateSourcePath)) {
      usedSourcePaths.add(candidateSourcePath);
      candidateIdentity.sourcePath = candidateSourcePath;
      return candidateIdentity;
    }
    suffix += 1;
    assetName = `${seedName}_${suffix}`;
  }
}

function duplicateSelectedRows() {
  const selectedRows = getSelectedRows();
  if (selectedRows.length === 0) {
    setStatus("请至少选择一行。", "error");
    return;
  }

  pushHistorySnapshot();
  const usedSourcePaths = new Set(state.rows.map((row) => row.identity.sourcePath));
  const selectedSet = new Set(state.selectedSourcePaths);
  const newRows = [];
  const duplicatedSourcePaths = [];

  state.rows.forEach((row) => {
    newRows.push(row);
    if (!selectedSet.has(row.identity.sourcePath)) {
      return;
    }

    const duplicated = cloneRow(row);
    duplicated.identity = buildUniqueIdentity({
      assetName: `${row.identity.assetName || "Monster"}_Copy`,
      categoryPath: row.identity.categoryPath,
    }, usedSourcePaths);
    duplicated.originalSourcePath = "";
    duplicated.persisted = false;
    duplicated.dirty = true;
    duplicated.issues = [];
    duplicated.hasErrors = false;
    duplicated.loadSucceeded = true;
    duplicated.loadError = "";
    duplicated.validationState = "stale";
    updateRowDerivedState(duplicated);
    duplicatedSourcePaths.push(duplicated.identity.sourcePath);
    newRows.push(duplicated);
  });

  state.rows = newRows;
  setSelection(duplicatedSourcePaths, duplicatedSourcePaths[0] || "");
  setStatus(`已复制 ${duplicatedSourcePaths.length} 行。`, "success");
}

function parseSaveAsTarget(inputValue, fallbackCategoryPath) {
  const normalized = normalizeCategoryPath(String(inputValue || "").trim());
  if (!normalized) {
    return null;
  }

  const lastSlashIndex = normalized.lastIndexOf("/");
  if (lastSlashIndex < 0) {
    return {
      categoryPath: normalizeCategoryPath(fallbackCategoryPath || "Combat/Monsters"),
      assetName: normalized,
    };
  }

  return {
    categoryPath: normalized.slice(0, lastSlashIndex),
    assetName: normalized.slice(lastSlashIndex + 1),
  };
}

async function saveAsSelectedRow() {
  const row = getSelectedRow();
  if (!row) {
    setStatus("请先选择一行。", "error");
    return;
  }

  const defaultTarget = `${row.identity.categoryPath || "Combat/Monsters"}/${row.identity.assetName || "Monster_Copy"}_Copy`;
  const rawTarget = window.prompt("请输入另存为目标（分类路径/资产名 或 仅资产名）", defaultTarget);
  if (rawTarget === null) {
    return;
  }

  const target = parseSaveAsTarget(rawTarget, row.identity.categoryPath);
  if (!target || !target.assetName || !target.categoryPath) {
    setStatus("另存为目标无效。", "error");
    return;
  }

  const draft = cloneRow(row);
  draft.identity.assetName = target.assetName;
  draft.identity.categoryPath = target.categoryPath;
  draft.originalSourcePath = "";
  draft.persisted = false;
  draft.dirty = true;
  updateRowDerivedState(draft);

  if (draft.identity.sourcePath !== row.identity.sourcePath && findRow(draft.identity.sourcePath)) {
    setStatus(`表格中已存在目标路径：${draft.identity.sourcePath}`, "error");
    return;
  }

  setStatus(`正在另存为 ${draft.identity.sourcePath}...`, "info");
  await fetchJson("/api/monster-config/save", {
    method: "POST",
    body: JSON.stringify(buildDocumentPayload(draft)),
  });

  await loadTable(draft.identity.sourcePath);
  setStatus("另存为完成。", "success");
}

async function deleteSelectedRows() {
  const selectedRows = getSelectedRows();
  if (selectedRows.length === 0) {
    setStatus("请至少选择一行。", "error");
    return;
  }

  const selectedCount = selectedRows.length;
  if (!window.confirm(`确认删除已选中的 ${selectedCount} 行吗？源资产与生成产物都会被移除。`)) {
    return;
  }

  const persistedRows = selectedRows.filter((row) => row.persisted || row.originalSourcePath);
  const transientRows = selectedRows.filter((row) => !row.persisted && !row.originalSourcePath);
  const selectedSet = new Set(state.selectedSourcePaths);

  if (transientRows.length > 0) {
    if (persistedRows.length === 0) {
      pushHistorySnapshot();
    }
    state.rows = state.rows.filter((row) => !selectedSet.has(row.identity.sourcePath) || row.persisted || row.originalSourcePath);
  }

  if (persistedRows.length > 0) {
    setStatus(`正在删除 ${selectedCount} 行...`, "info");
    const payload = await fetchJson("/api/monster-configs/delete", {
      method: "POST",
      body: JSON.stringify({ sourcePaths: persistedRows.map(getDeleteSourcePath) }),
    });

    renderResult(payload);
    if (payload.hasFailures) {
      setStatus("删除完成，但存在失败项。", "error");
      await loadTable();
      return;
    }
  }

  if (persistedRows.length > 0) {
    await loadTable();
  } else {
    const nextRow = state.rows[0];
    setSelection(nextRow ? [nextRow.identity.sourcePath] : [], nextRow ? nextRow.identity.sourcePath : "");
  }

  setStatus(`已删除 ${selectedCount} 行。`, "success");
}

function syncInspectorToSelectedRow() {
  const row = getSelectedRow();
  if (!row || isHydratingInspector) {
    return;
  }

  const nextIdentity = {
    assetName: ui.fields.assetName.value.trim(),
    categoryPath: normalizeCategoryPath(ui.fields.categoryPath.value.trim()),
  };
  const nextModel = {
    monsterTemplateId: readUInt(ui.fields.monsterTemplateId.value),
    debugName: ui.fields.debugName.value.trim(),
    sceneId: readUInt(ui.fields.sceneId.value),
    primarySkillId: readUInt(ui.fields.primarySkillId.value),
    currentHealth: readUInt(ui.fields.currentHealth.value),
    maxHealth: readUInt(ui.fields.maxHealth.value),
    attackPower: readUInt(ui.fields.attackPower.value),
    defensePower: readUInt(ui.fields.defensePower.value),
    experienceReward: readUInt(ui.fields.experienceReward.value),
    goldReward: readUInt(ui.fields.goldReward.value),
  };

  const beforeSignature = JSON.stringify({
    assetName: row.identity.assetName,
    categoryPath: row.identity.categoryPath,
    monsterTemplateId: row.model.monsterTemplateId,
    debugName: row.model.debugName,
    sceneId: row.model.spawnParams.sceneId,
    primarySkillId: row.model.spawnParams.primarySkillId,
    currentHealth: row.model.spawnParams.currentHealth,
    maxHealth: row.model.spawnParams.maxHealth,
    attackPower: row.model.spawnParams.attackPower,
    defensePower: row.model.spawnParams.defensePower,
    experienceReward: row.model.spawnParams.experienceReward,
    goldReward: row.model.spawnParams.goldReward,
  });
  const afterSignature = JSON.stringify({
    assetName: nextIdentity.assetName,
    categoryPath: nextIdentity.categoryPath,
    monsterTemplateId: nextModel.monsterTemplateId,
    debugName: nextModel.debugName,
    sceneId: nextModel.sceneId,
    primarySkillId: nextModel.primarySkillId,
    currentHealth: nextModel.currentHealth,
    maxHealth: nextModel.maxHealth,
    attackPower: nextModel.attackPower,
    defensePower: nextModel.defensePower,
    experienceReward: nextModel.experienceReward,
    goldReward: nextModel.goldReward,
  });
  if (beforeSignature === afterSignature) {
    return;
  }

  pushHistorySnapshot();
  const previousSourcePath = row.identity.sourcePath;
  row.identity.assetName = nextIdentity.assetName;
  row.identity.categoryPath = nextIdentity.categoryPath;
  row.model.monsterTemplateId = nextModel.monsterTemplateId;
  row.model.debugName = nextModel.debugName;
  row.model.spawnParams.sceneId = nextModel.sceneId;
  row.model.spawnParams.primarySkillId = nextModel.primarySkillId;
  row.model.spawnParams.currentHealth = nextModel.currentHealth;
  row.model.spawnParams.maxHealth = nextModel.maxHealth;
  row.model.spawnParams.attackPower = nextModel.attackPower;
  row.model.spawnParams.defensePower = nextModel.defensePower;
  row.model.spawnParams.experienceReward = nextModel.experienceReward;
  row.model.spawnParams.goldReward = nextModel.goldReward;
  updateRowDerivedState(row);
  markRowDirty(row);

  state.selectedSourcePath = row.identity.sourcePath;
  state.selectedSourcePaths = state.selectedSourcePaths.map((sourcePath) => (sourcePath === previousSourcePath ? row.identity.sourcePath : sourcePath));
  state.selectionAnchorSourcePath = row.identity.sourcePath;
  if (state.activeCell && state.activeCell.sourcePath === previousSourcePath) {
    state.activeCell.sourcePath = row.identity.sourcePath;
  }
  renderAll();
}

function addSkillToSelectedRow() {
  const row = getSelectedRow();
  if (!row) {
    setStatus("请先选择一行。", "error");
    return;
  }

  const skillId = readUInt(ui.skillIdInput.value);
  if (skillId === 0) {
    setStatus("技能 Id 必须大于 0。", "error");
    return;
  }
  if (row.model.skillIds.includes(skillId)) {
    setStatus(`技能 Id ${skillId} 已存在。`, "error");
    return;
  }

  pushHistorySnapshot();
  row.model.skillIds.push(skillId);
  markRowDirty(row);
  ui.skillIdInput.value = "";
  renderAll();
  setStatus(`已添加技能 Id ${skillId}。`, "success");
}

function removeSkillFromSelectedRow(index) {
  const row = getSelectedRow();
  if (!row) {
    return;
  }
  if (index < 0 || index >= row.model.skillIds.length) {
    return;
  }

  pushHistorySnapshot();
  row.model.skillIds.splice(index, 1);
  markRowDirty(row);
  renderAll();
  setStatus("已移除技能。", "success");
}

function parseClipboardGrid(text) {
  const normalized = String(text || "").replace(/\r/g, "");
  const rows = normalized.split("\n");
  if (rows.length > 1 && rows[rows.length - 1] === "") {
    rows.pop();
  }
  return rows.map((row) => row.split("\t"));
}

function handleTablePaste(event, input) {
  const clipboardText = event.clipboardData?.getData("text/plain") || "";
  const grid = parseClipboardGrid(clipboardText);
  const rowCount = grid.length;
  const columnCount = grid.reduce((max, row) => Math.max(max, row.length), 0);
  const selectedRows = getSelectedRows();
  const shouldBroadcastSingleValue = selectedRows.length > 1 && rowCount === 1 && columnCount === 1;
  const shouldApplyGrid = rowCount > 1 || columnCount > 1 || shouldBroadcastSingleValue;

  if (!shouldApplyGrid) {
    return;
  }

  event.preventDefault();

  const startSourcePath = input.dataset.sourcePath;
  const startField = input.dataset.field;
  const visibleRows = getVisibleRows();
  const startRowIndex = visibleRows.findIndex((row) => row.identity.sourcePath === startSourcePath);
  const startFieldIndex = EDITABLE_FIELD_KEYS.indexOf(startField);
  if (startRowIndex < 0 || startFieldIndex < 0) {
    return;
  }

  if (shouldBroadcastSingleValue) {
    pushHistorySnapshot();
    applyFieldValueToRows(selectedRows, startField, grid[0][0]);
    renderAll();
    setStatus(`已将${getFieldLabel(startField)}应用到 ${selectedRows.length} 个选中行。`, "success");
    return;
  }

  pushHistorySnapshot();
  for (let rowOffset = 0; rowOffset < grid.length; rowOffset += 1) {
    const targetRow = visibleRows[startRowIndex + rowOffset];
    if (!targetRow) {
      break;
    }

    for (let fieldOffset = 0; fieldOffset < grid[rowOffset].length; fieldOffset += 1) {
      const targetField = EDITABLE_FIELD_KEYS[startFieldIndex + fieldOffset];
      if (!targetField) {
        break;
      }
      updateTableField(targetRow, targetField, grid[rowOffset][fieldOffset]);
    }
  }

  renderAll();
  setStatus(`已粘贴 ${grid.length} x ${columnCount} 个单元格。`, "success");
}

function handleTableFieldCommit(input) {
  const row = findRow(input.dataset.sourcePath);
  if (!row) {
    return;
  }

  const selectedRows = getSelectedRows();
  const targetRows = isRowSelected(row.identity.sourcePath) && selectedRows.length > 1
    ? selectedRows
    : [row];
  const nextValue = NUMERIC_FIELD_KEYS.has(input.dataset.field) ? readUInt(input.value) : String(input.value ?? "").trim();
  const hasChanges = targetRows.some((targetRow) => getFieldValue(targetRow, input.dataset.field) !== nextValue);
  if (!hasChanges) {
    return;
  }

  pushHistorySnapshot();
  applyFieldValueToRows(targetRows, input.dataset.field, input.value);
  renderAll();
}

function sortBy(fieldName) {
  if (state.sort.field === fieldName) {
    state.sort.direction = state.sort.direction === "asc" ? "desc" : "asc";
  } else {
    state.sort.field = fieldName;
    state.sort.direction = "asc";
  }
  renderAll();
}

function getActiveEditableField() {
  if (!state.activeCell || !EDITABLE_FIELD_KEYS.includes(state.activeCell.field)) {
    return "";
  }
  return state.activeCell.field;
}

function fillDownSelection() {
  const fieldName = getActiveEditableField();
  if (!fieldName) {
    setStatus("请先选中一个可编辑单元格。", "error");
    return;
  }

  const selectedRows = getSelectedRowsInVisibleOrder();
  if (selectedRows.length < 2) {
    setStatus("向下填充至少需要选择 2 行。", "error");
    return;
  }

  pushHistorySnapshot();
  const sourceRow = selectedRows.find((row) => row.identity.sourcePath === state.activeCell.sourcePath) || selectedRows[0];
  const sourceValue = getFieldValue(sourceRow, fieldName);
  selectedRows.forEach((row) => {
    if (row !== sourceRow) {
      updateTableField(row, fieldName, sourceValue);
    }
  });

  renderAll();
  setStatus(`已向下填充 ${selectedRows.length} 行的${getFieldLabel(fieldName)}列。`, "success");
}

function incrementFillSelection() {
  const fieldName = getActiveEditableField();
  if (!fieldName) {
    setStatus("请先选中一个数值单元格。", "error");
    return;
  }
  if (!NUMERIC_FIELD_KEYS.has(fieldName)) {
    setStatus("递增填充只支持数值列。", "error");
    return;
  }

  const selectedRows = getSelectedRowsInVisibleOrder();
  if (selectedRows.length < 2) {
    setStatus("递增填充至少需要选择 2 行。", "error");
    return;
  }

  pushHistorySnapshot();
  const firstValue = Number(getFieldValue(selectedRows[0], fieldName));
  const secondValue = Number(getFieldValue(selectedRows[1], fieldName));
  const step = secondValue !== firstValue ? (secondValue - firstValue) : 1;

  selectedRows.forEach((row, index) => {
    const nextValue = firstValue + (step * index);
    updateTableField(row, fieldName, String(nextValue));
  });

  renderAll();
  setStatus(`已对 ${selectedRows.length} 行执行${getFieldLabel(fieldName)}递增填充。`, "success");
}

function beginColumnResize(event, columnKey) {
  event.preventDefault();
  event.stopPropagation();

  const column = COLUMN_BY_KEY[columnKey];
  if (!column) {
    return;
  }

  resizeSession = {
    columnKey,
    startX: event.clientX,
    startWidth: state.columnWidths[columnKey] || column.width,
    minWidth: column.minWidth || 60,
  };
  document.body.classList.add("is-resizing-columns");
}

function handleColumnResizeMove(event) {
  if (!resizeSession) {
    return;
  }

  const delta = event.clientX - resizeSession.startX;
  state.columnWidths[resizeSession.columnKey] = Math.max(resizeSession.minWidth, resizeSession.startWidth + delta);
  renderTableChrome();
}

function endColumnResize() {
  if (!resizeSession) {
    return;
  }
  resizeSession = null;
  document.body.classList.remove("is-resizing-columns");
}

function bindUi() {
  ui.selectedAssetLabel = $("selected-asset-label");
  ui.selectedCountLabel = $("selected-count-label");
  ui.connectionBadge = $("connection-badge");
  ui.dirtyCountLabel = $("dirty-count-label");
  ui.assetTabs = Array.from(document.querySelectorAll("[data-asset-tab]"));
  ui.selectAllCheckbox = $("select-all-checkbox");
  ui.tableFilter = $("table-filter");
  ui.tableBody = $("table-body");
  ui.tableColgroup = $("table-colgroup");
  ui.configTable = document.querySelector(".config-table");
  ui.inspectorTitle = $("inspector-title");
  ui.statusCard = $("status-card");
  ui.resultCard = $("result-card");
  ui.issueList = $("issue-list");
  ui.skillIdList = $("skill-id-list");
  ui.skillIdInput = $("skill-id-input");
  ui.generatedJsonPath = $("generated-json-path");
  ui.generatedMobPath = $("generated-mob-path");
  ui.generatedRoundtripPath = $("generated-roundtrip-path");
  ui.publishMobPath = $("publish-mob-path");
  ui.clearSelectedCells = $("clear-selected-cells");
  ui.undoAction = $("undo-action");
  ui.redoAction = $("redo-action");

  ui.fields = {
    assetName: $("asset-name"),
    categoryPath: $("category-path"),
    sourcePath: $("source-path"),
    monsterTemplateId: $("monster-template-id"),
    debugName: $("debug-name"),
    sceneId: $("scene-id"),
    primarySkillId: $("primary-skill-id"),
    currentHealth: $("current-health"),
    maxHealth: $("max-health"),
    attackPower: $("attack-power"),
    defensePower: $("defense-power"),
    experienceReward: $("experience-reward"),
    goldReward: $("gold-reward"),
  };

  $("reload-table").addEventListener("click", async () => {
    try {
      setStatus("正在刷新表格...", "info");
      await loadTable(state.selectedSourcePath);
      setStatus("表格已刷新。", "success");
    } catch (error) {
      setStatus(error.message, "error");
    }
  });
  $("add-row").addEventListener("click", addNewRow);
  $("duplicate-selected").addEventListener("click", duplicateSelectedRows);
  $("save-as-selected").addEventListener("click", () => saveAsSelectedRow().catch((error) => setStatus(error.message, "error")));
  $("delete-selected").addEventListener("click", () => deleteSelectedRows().catch((error) => setStatus(error.message, "error")));
  $("save-all").addEventListener("click", () => saveAllDirtyRows().catch((error) => setStatus(error.message, "error")));
  $("validate-selected").addEventListener("click", () => validateSelectedRow().catch((error) => setStatus(error.message, "error")));
  $("export-selected").addEventListener("click", () => exportSelectedRow(false).catch((error) => setStatus(error.message, "error")));
  $("publish-selected").addEventListener("click", () => exportSelectedRow(true).catch((error) => setStatus(error.message, "error")));
  $("fill-down").addEventListener("click", fillDownSelection);
  $("increment-fill").addEventListener("click", incrementFillSelection);
  ui.clearSelectedCells.addEventListener("click", clearSelectedCells);
  ui.undoAction.addEventListener("click", undoLastAction);
  ui.redoAction.addEventListener("click", redoLastAction);
  $("add-skill-id").addEventListener("click", addSkillToSelectedRow);
  $("select-all-visible").addEventListener("click", () => setSelection(getVisibleSourcePaths(), state.selectedSourcePath || getVisibleSourcePaths()[0] || ""));
  $("clear-selection").addEventListener("click", () => setSelection([], ""));

  ui.assetTabs.forEach((button) => {
    button.addEventListener("click", () => handleAssetTabClick(button.dataset.assetTab));
  });

  ui.selectAllCheckbox.addEventListener("change", () => {
    if (ui.selectAllCheckbox.checked) {
      const visibleSourcePaths = getVisibleSourcePaths();
      setSelection(visibleSourcePaths, visibleSourcePaths[0] || "");
      return;
    }
    setSelection([], "");
  });

  ui.skillIdInput.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      addSkillToSelectedRow();
    }
  });

  ui.tableFilter.addEventListener("input", () => {
    state.filterText = ui.tableFilter.value.trim().toLowerCase();
    renderAll();
  });

  document.querySelectorAll("[data-sort-field]").forEach((button) => {
    button.addEventListener("click", () => sortBy(button.dataset.sortField));
  });

  document.querySelectorAll("[data-resize-column]").forEach((handle) => {
    handle.addEventListener("pointerdown", (event) => beginColumnResize(event, handle.dataset.resizeColumn));
  });
  window.addEventListener("pointermove", handleColumnResizeMove);
  window.addEventListener("pointerup", endColumnResize);
  window.addEventListener("pointercancel", endColumnResize);

  ui.tableBody.addEventListener("click", (event) => {
    const checkbox = event.target.closest("[data-select-checkbox]");
    if (checkbox) {
      selectRow(checkbox.dataset.selectCheckbox, {
        additive: event.metaKey || event.ctrlKey,
        range: event.shiftKey,
      });
      return;
    }

    const selectButton = event.target.closest("[data-select-row]");
    if (selectButton) {
      selectRow(selectButton.dataset.selectRow, {
        additive: event.metaKey || event.ctrlKey,
        range: event.shiftKey,
      });
      return;
    }

    const rowElement = event.target.closest("tr[data-source-path]");
    if (rowElement && !event.target.closest(".table-input")) {
      selectRow(rowElement.dataset.sourcePath, {
        additive: event.metaKey || event.ctrlKey,
        range: event.shiftKey,
      });
    }
  });

  ui.tableBody.addEventListener("focusin", (event) => {
    const input = event.target.closest(".table-input");
    if (!input) {
      return;
    }

    const sourcePath = input.dataset.sourcePath;
    const fieldName = input.dataset.field;
    setActiveCell(sourcePath, fieldName);
    if (!isRowSelected(sourcePath)) {
      state.selectedSourcePaths = [sourcePath];
      state.selectedSourcePath = sourcePath;
      state.selectionAnchorSourcePath = sourcePath;
      renderAll();

      const focusedInput = findCellInput(sourcePath, fieldName);
      if (focusedInput) {
        focusedInput.focus({ preventScroll: true });
        focusedInput.select();
      }
    }
  });

  ui.tableBody.addEventListener("change", (event) => {
    const input = event.target.closest(".table-input");
    if (!input) {
      return;
    }
    handleTableFieldCommit(input);
  });

  ui.tableBody.addEventListener("keydown", (event) => {
    const input = event.target.closest(".table-input");
    if (!input) {
      return;
    }

    setActiveCell(input.dataset.sourcePath, input.dataset.field);

    switch (event.key) {
    case "ArrowUp":
      event.preventDefault();
      moveActiveCell(-1, 0);
      break;
    case "ArrowDown":
      event.preventDefault();
      moveActiveCell(1, 0);
      break;
    case "ArrowLeft":
      event.preventDefault();
      moveActiveCell(0, -1);
      break;
    case "ArrowRight":
      event.preventDefault();
      moveActiveCell(0, 1);
      break;
    case "Enter":
      event.preventDefault();
      moveActiveCell(event.shiftKey ? -1 : 1, 0);
      break;
    case "Tab":
      event.preventDefault();
      moveActiveCell(0, event.shiftKey ? -1 : 1);
      break;
    default:
      break;
    }
  });

  ui.tableBody.addEventListener("paste", (event) => {
    const input = event.target.closest(".table-input");
    if (!input) {
      return;
    }
    handleTablePaste(event, input);
  });

  ui.skillIdList.addEventListener("click", (event) => {
    const button = event.target.closest("[data-remove-skill]");
    if (!button) {
      return;
    }
    removeSkillFromSelectedRow(Number.parseInt(button.dataset.removeSkill, 10));
  });

  Object.values(ui.fields).forEach((field) => {
    field.addEventListener("input", () => {
      if (!isHydratingInspector) {
        syncInspectorToSelectedRow();
      }
    });
  });

  document.addEventListener("keydown", (event) => {
    const isMeta = event.metaKey || event.ctrlKey;
    if (!isMeta || event.altKey) {
      return;
    }

    if (event.key.toLowerCase() === "z" && !event.shiftKey) {
      event.preventDefault();
      undoLastAction();
      return;
    }

    if (event.key.toLowerCase() === "y" || (event.key.toLowerCase() === "z" && event.shiftKey)) {
      event.preventDefault();
      redoLastAction();
    }
  });
}

async function init() {
  bindUi();
  clearInspector();

  try {
    await fetchJson("/api/status");
    setConnectionState(true);
    setStatus("服务在线，正在加载表格...", "success");
    await loadTable();
  } catch (error) {
    setConnectionState(false);
    setStatus(error.message, "error");
  }
}

document.addEventListener("DOMContentLoaded", init);
