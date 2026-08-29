"""Writes a small DDNet map with the quad layers Block zones look for.

The stock maps in data/maps have no "game_zones" group, so a 1on1 cannot even be
started on them ("This map does not have any 1on1 spawn positions defined") and
no event has anywhere to spawn its participants. Rather than ship a binary
fixture, the harness generates one: the format is the plain DDNet datafile
(see CDataFileWriter::Finish in src/engine/shared/datafile.cpp).
"""

import struct
import zlib

# src/game/mapitems.h
MAPITEMTYPE_VERSION = 0
MAPITEMTYPE_INFO = 1
MAPITEMTYPE_GROUP = 2 + 2  # MAPITEMTYPE_GROUP == 4
MAPITEMTYPE_LAYER = 5
LAYERTYPE_TILES = 2
LAYERTYPE_QUADS = 3
TILESLAYERFLAG_GAME = 1
TILE_AIR = 0
TILE_SOLID = 1
ENTITY_OFFSET = 255 - 16 * 4
ENTITY_SPAWN = 1

TILE_SIZE = 32


def str_to_ints(text, num_ints):
    """Mirror of StrToInts (src/game/gamecore.cpp)."""
    raw = text.encode() + b"\0"
    assert len(raw) <= num_ints * 4, f"{text!r} does not fit in {num_ints} ints"
    out = []
    for i in range(num_ints):
        chunk = [raw[i * 4 + c] if i * 4 + c < len(raw) else 0 for c in range(4)]
        out.append(
            ((chunk[0] + 128) << 24) | ((chunk[1] + 128) << 16) | ((chunk[2] + 128) << 8) | (chunk[3] + 128)
        )
    out[-1] &= 0xFFFFFF00
    return [v - (1 << 32) if v >= (1 << 31) else v for v in out]


def f2fx(value):
    """22.10 fixed point, the inverse of fx2f."""
    return int(round(value * 1024))


class Writer:
    """Just enough of CDataFileWriter to produce a loadable map."""

    def __init__(self):
        self.items = []  # (type, id, payload bytes)
        self.datas = []  # uncompressed bytes

    def add_data(self, payload):
        self.datas.append(payload)
        return len(self.datas) - 1

    def add_item(self, type_id, item_id, ints):
        self.items.append((type_id, item_id, struct.pack(f"<{len(ints)}i", *ints)))

    def to_bytes(self):
        # items are written grouped by type, in the order the types first appear
        types = []
        for type_id, _, _ in self.items:
            if type_id not in types:
                types.append(type_id)
        ordered = []
        for t in types:
            ordered.extend([it for it in self.items if it[0] == t])

        compressed = [zlib.compress(d, 9) for d in self.datas]
        item_size = sum(len(p) + 8 for _, _, p in ordered)
        data_size = sum(len(c) for c in compressed)
        types_size = len(types) * 12
        header_size = 36
        offset_size = (len(ordered) + len(compressed) * 2) * 4
        swap_size = header_size + types_size + offset_size + item_size
        file_size = swap_size + data_size
        size_offset = 16

        out = bytearray()
        out += b"DATA"
        out += struct.pack(
            "<8i",
            4,
            file_size - size_offset,
            swap_size - size_offset,
            len(types),
            len(ordered),
            len(compressed),
            item_size,
            data_size,
        )

        count = 0
        for t in types:
            num = sum(1 for it in ordered if it[0] == t)
            out += struct.pack("<3i", t, count, num)
            count += num

        offset = 0
        for _, _, payload in ordered:
            out += struct.pack("<i", offset)
            offset += len(payload) + 8

        offset = 0
        for c in compressed:
            out += struct.pack("<i", offset)
            offset += len(c)
        for d in self.datas:
            out += struct.pack("<i", len(d))

        for type_id, item_id, payload in ordered:
            out += struct.pack("<2i", (type_id << 16) | item_id, len(payload))
            out += payload

        for c in compressed:
            out += c
        return bytes(out)


def _quad(cx, cy, half_w=48, half_h=48):
    """One quad centred on (cx, cy) in pixels, as CQuad's 38 ints."""
    corners = [
        (cx - half_w, cy - half_h),
        (cx + half_w, cy - half_h),
        (cx - half_w, cy + half_h),
        (cx + half_w, cy + half_h),
    ]
    ints = []
    for x, y in corners:
        ints += [f2fx(x), f2fx(y)]
    ints += [f2fx(cx), f2fx(cy)]  # m_aPoints[4], the pivot
    for _ in range(4):  # m_aColors
        ints += [255, 255, 255, 255]
    ints += [0, 0, 1024, 0, 0, 1024, 1024, 1024]  # m_aTexcoords
    ints += [-1, 0, -1, 0]  # pos env, offset, colour env, offset
    return ints


# The quad layers the harness relies on, each with the positions it should offer.
# Coordinates are in tiles; everything sits on the floor row so a spawned tee
# does not fall out of the world.
FLOOR_ROW = 20
QUAD_LAYERS = [
    ("1on1_prep", [(10, FLOOR_ROW - 1), (14, FLOOR_ROW - 1)]),
    ("1on1_a1", [(30, FLOOR_ROW - 1), (40, FLOOR_ROW - 1)]),
    ("tdm_spawn", [(50, FLOOR_ROW - 1), (54, FLOOR_ROW - 1)]),
    ("lmb_spawn", [(58, FLOOR_ROW - 1), (62, FLOOR_ROW - 1)]),
    ("zcb_spawn", [(66, FLOOR_ROW - 1)]),
    ("zcg_spawn", [(70, FLOOR_ROW - 1)]),
]

WIDTH = 80
HEIGHT = 25


def build():
    """Returns the bytes of a map called block_zones."""
    w = Writer()
    w.add_item(MAPITEMTYPE_VERSION, 0, [1])

    tiles = bytearray(WIDTH * HEIGHT * 4)

    def set_tile(tx, ty, index):
        off = (ty * WIDTH + tx) * 4
        tiles[off] = index
        tiles[off + 1] = 0  # flags
        tiles[off + 2] = 0  # skip
        tiles[off + 3] = 0  # must be 0

    for x in range(WIDTH):
        for y in range(FLOOR_ROW, HEIGHT):
            set_tile(x, y, TILE_SOLID)
    # a couple of ordinary spawn tiles so players who are not in an event or a
    # duel still have somewhere to go
    for x in (4, 5, 6):
        set_tile(x, FLOOR_ROW - 1, ENTITY_OFFSET + ENTITY_SPAWN)

    tile_data = w.add_data(bytes(tiles))

    # group 0: the game group
    w.add_item(MAPITEMTYPE_GROUP, 0, [3, 0, 0, 100, 100, 0, 1, 0, 0, 0, 0, 0] + str_to_ints("Game", 3))
    w.add_item(
        MAPITEMTYPE_LAYER,
        0,
        [0, LAYERTYPE_TILES, 0]  # CMapItemLayer
        + [2, WIDTH, HEIGHT, TILESLAYERFLAG_GAME]  # version, w, h, flags
        + [255, 255, 255, 255, -1, 0]  # colour, colour env, offset
        + [-1, tile_data],  # image, data
    )

    # group 1: the Block zones
    quad_layer_ids = []
    for name, positions in QUAD_LAYERS:
        ints = []
        for tx, ty in positions:
            ints += _quad(tx * TILE_SIZE + TILE_SIZE // 2, ty * TILE_SIZE + TILE_SIZE // 2)
        quad_layer_ids.append((name, len(positions), w.add_data(struct.pack(f"<{len(ints)}i", *ints))))

    w.add_item(
        MAPITEMTYPE_GROUP,
        1,
        [3, 0, 0, 100, 100, 1, len(quad_layer_ids), 0, 0, 0, 0, 0] + str_to_ints("game_zones", 3),
    )
    for i, (name, num_quads, data_id) in enumerate(quad_layer_ids):
        w.add_item(
            MAPITEMTYPE_LAYER,
            1 + i,
            [0, LAYERTYPE_QUADS, 0]  # CMapItemLayer
            + [2, num_quads, data_id, -1]  # version, num quads, data, image
            + str_to_ints(name, 3),
        )

    return w.to_bytes()


def write(path):
    with open(path, "wb") as f:
        f.write(build())
    return path


if __name__ == "__main__":
    import sys

    print(write(sys.argv[1] if len(sys.argv) > 1 else "block_zones.map"))
