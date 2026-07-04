#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 / 合并 assert/item_registry.json。

扫描 assert/minecraft/textures/item/*.png，为每个图标生成一条物品定义。
合并式：已存在的条目保留（不覆盖手工编辑），只补新增图标。
少量"调试子集"条目（DEBUG_OVERRIDES）会被强制写入 / 更新，标记
load_in_debug=true 并设置正确的 block_type / behavior，供调试模式只加载它们。

用法（在仓库根目录 E:\\opengl\\iMc 运行）：
    python tools/gen_item_registry.py
"""
import json
import os
import sys

REPO_ROOT   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON_DIR    = os.path.join(REPO_ROOT, "assert", "minecraft", "textures", "item")
ICON_REL    = "assert/minecraft/textures/item"   # 写进 JSON 的工作目录相对路径
OUT_PATH    = os.path.join(REPO_ROOT, "assert", "item_registry.json")

# 工具名 token（含则判为 TOOL，有耐久、不可堆叠）
TOOL_TOKENS = ("sword", "pickaxe", "axe", "shovel", "hoe")
# 食物名单（简表，命中则 FOOD）
FOOD_NAMES = {
    "apple", "golden_apple", "enchanted_golden_apple", "bread", "carrot",
    "golden_carrot", "potato", "baked_potato", "beetroot", "beetroot_soup",
    "beef", "cooked_beef", "porkchop", "cooked_porkchop", "chicken",
    "cooked_chicken", "mutton", "cooked_mutton", "rabbit", "cooked_rabbit",
    "cod", "cooked_cod", "salmon", "cooked_salmon", "melon_slice", "cookie",
    "pumpkin_pie", "mushroom_stew", "rabbit_stew", "sweet_berries", "honey_bottle",
    "dried_kelp", "tropical_fish", "pufferfish", "rotten_flesh", "spider_eye",
}

# 调试子集：id -> 覆盖字段。这些条目一定 load_in_debug=true。
# 方块物品设 behavior=block + 对应 block_type，可右键放置到世界。
# 方块纹理目录（方块物品图标从这里取，item 目录里没有这些方块的图标）
BLOCK_REL = "assert/minecraft/textures/block"

DEBUG_OVERRIDES = {
    "stone":       {"category": "block", "behavior": "block", "block_type": "stone",  "max_stack": 64,
                    "icon": f"{BLOCK_REL}/stone.png"},
    "dirt":        {"category": "block", "behavior": "block", "block_type": "dirt",   "max_stack": 64,
                    "icon": f"{BLOCK_REL}/dirt.png"},
    "sand":        {"category": "block", "behavior": "block", "block_type": "sand",   "max_stack": 64,
                    "icon": f"{BLOCK_REL}/sand.png"},
    "oak_log":     {"category": "block", "behavior": "block", "block_type": "wood",   "max_stack": 64,
                    "icon": f"{BLOCK_REL}/oak_log.png"},
    "apple":       {"category": "food",  "behavior": "generic", "max_stack": 64},
    "stick":       {"category": "material", "behavior": "generic", "max_stack": 64},
    "iron_ingot":  {"category": "material", "behavior": "generic", "max_stack": 64},
    "diamond":     {"category": "material", "behavior": "generic", "max_stack": 64},
    "spyglass":    {"category": "misc",  "behavior": "spyglass", "max_stack": 1},
    "diamond_sword": {"category": "tool", "behavior": "generic",
                      "has_durability": True, "max_durability": 1561, "max_stack": 1},
}


def prettify(item_id):
    return " ".join(w.capitalize() for w in item_id.split("_"))


def default_entry(item_id):
    is_tool = any(tok in item_id for tok in TOOL_TOKENS)
    is_food = item_id in FOOD_NAMES
    if is_tool:
        category, max_stack = "tool", 1
        has_dur, max_dur = True, 250
    elif is_food:
        category, max_stack = "food", 64
        has_dur, max_dur = False, 0
    else:
        category, max_stack = "misc", 64
        has_dur, max_dur = False, 0
    return {
        "id": item_id,
        "display_name": prettify(item_id),
        "icon_name": item_id,
        "icon": f"{ICON_REL}/{item_id}.png",
        "category": category,
        "max_stack": max_stack,
        "has_durability": has_dur,
        "max_durability": max_dur,
        "model_type": "extruded_2d",
        "block_type": "air",
        "behavior": "generic",
        "load_in_debug": False,
    }


def main():
    if not os.path.isdir(ICON_DIR):
        print(f"[gen] icon dir not found: {ICON_DIR}", file=sys.stderr)
        sys.exit(1)

    # 载入已有条目（保留手工编辑）
    existing = {}
    if os.path.isfile(OUT_PATH):
        try:
            with open(OUT_PATH, "r", encoding="utf-8-sig") as f:
                data = json.load(f)
            items = data if isinstance(data, list) else data.get("items", [])
            for it in items:
                if "id" in it:
                    existing[it["id"]] = it
        except Exception as e:
            print(f"[gen] warn: failed to read existing registry ({e}), regenerating", file=sys.stderr)

    pngs = sorted(fn[:-4] for fn in os.listdir(ICON_DIR) if fn.lower().endswith(".png"))

    added = 0
    for item_id in pngs:
        if item_id not in existing:
            existing[item_id] = default_entry(item_id)
            added += 1

    # 应用调试子集覆盖（这些 id 若无图标也强制写入，交由存在性决定；此处仅覆盖已存在的）
    for item_id, over in DEBUG_OVERRIDES.items():
        if item_id not in existing:
            # 调试项对应图标缺失也建条目（后续可换图标）
            existing[item_id] = default_entry(item_id)
        entry = existing[item_id]
        entry.update(over)
        entry["load_in_debug"] = True

    merged = [existing[k] for k in sorted(existing.keys())]

    # 写出：CRLF + UTF-8，缩进 2
    text = json.dumps(merged, ensure_ascii=False, indent=2)
    with open(OUT_PATH, "w", encoding="utf-8", newline="\r\n") as f:
        f.write(text)
        f.write("\n")

    debug_n = sum(1 for it in merged if it.get("load_in_debug"))
    print(f"[gen] scanned {len(pngs)} icons, total {len(merged)} entries "
          f"(+{added} new), {debug_n} marked load_in_debug -> {OUT_PATH}")


if __name__ == "__main__":
    main()
