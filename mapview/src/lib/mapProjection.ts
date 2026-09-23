import type { MapProjection, Point2D } from "./types";

/**
 * Projection parameters describing how Unreal world coordinates (centimetres)
 * are mapped onto the static StarRupture map image.
 *
 * The plugin sends these in `GET /cargo` under `map`; the fallback below keeps
 * the viewer usable offline and must stay in sync with `map_state_types.h`.
 */
export interface MapProjectionConstants {
    src_x1: number;
    src_y1: number;
    src_x2: number;
    src_y2: number;
    dst_x1: number;
    dst_y1: number;
    dst_x2: number;
    dst_y2: number;
    content_width: number;
    content_height: number;
    image_width: number;
    image_height: number;
}

const SRC_X1 = -358583;
const SRC_Y1 = -263782;
const SRC_X2 = -98583;
const SRC_Y2 = -9439;
const DST_X1 = 1518.414983;
const DST_Y1 = 2272.832995;
const DST_X2 = 6895.078786;
const DST_Y2 = 7535.110312;

export const DEFAULT_MAP_PROJECTION: MapProjectionConstants = {
    src_x1: SRC_X1,
    src_y1: SRC_Y1,
    src_x2: SRC_X2,
    src_y2: SRC_Y2,
    dst_x1: DST_X1,
    dst_y1: DST_Y1,
    dst_x2: DST_X2,
    dst_y2: DST_Y2,
    content_width: DST_X2 - DST_X1,
    content_height: DST_Y2 - DST_Y1,
    image_width: 9019,
    image_height: 11691,
};

function pickNumber(value: unknown, fallback: number): number {
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : fallback;
}

/** Completes a partial projection payload with the built-in constants. */
export function resolveMapProjection(
    map?: MapProjection | null,
): MapProjectionConstants {
    if (!map) return DEFAULT_MAP_PROJECTION;

    const resolved: MapProjectionConstants = {
        src_x1: pickNumber(map.src_x1, DEFAULT_MAP_PROJECTION.src_x1),
        src_y1: pickNumber(map.src_y1, DEFAULT_MAP_PROJECTION.src_y1),
        src_x2: pickNumber(map.src_x2, DEFAULT_MAP_PROJECTION.src_x2),
        src_y2: pickNumber(map.src_y2, DEFAULT_MAP_PROJECTION.src_y2),
        dst_x1: pickNumber(map.dst_x1, DEFAULT_MAP_PROJECTION.dst_x1),
        dst_y1: pickNumber(map.dst_y1, DEFAULT_MAP_PROJECTION.dst_y1),
        dst_x2: pickNumber(map.dst_x2, DEFAULT_MAP_PROJECTION.dst_x2),
        dst_y2: pickNumber(map.dst_y2, DEFAULT_MAP_PROJECTION.dst_y2),
        content_width: pickNumber(
            map.content_width,
            DEFAULT_MAP_PROJECTION.content_width,
        ),
        content_height: pickNumber(
            map.content_height,
            DEFAULT_MAP_PROJECTION.content_height,
        ),
        image_width: pickNumber(
            map.image_width,
            DEFAULT_MAP_PROJECTION.image_width,
        ),
        image_height: pickNumber(
            map.image_height,
            DEFAULT_MAP_PROJECTION.image_height,
        ),
    };

    if (resolved.src_x2 === resolved.src_x1 || resolved.src_y2 === resolved.src_y1) {
        return DEFAULT_MAP_PROJECTION;
    }
    return resolved;
}

/** Map units per world centimetre. */
export function mapProjectionScale(projection: MapProjectionConstants): Point2D {
    return {
        x: (projection.dst_x2 - projection.dst_x1) / (projection.src_x2 - projection.src_x1),
        y: (projection.dst_y2 - projection.dst_y1) / (projection.src_y2 - projection.src_y1),
    };
}

/**
 * Same computation as `MapStateRuntime::Detail::WorldToMap`.
 * `world` must be expressed in Unreal centimetres.
 */
export function worldToMap(
    world: { x: number; y: number },
    projection: MapProjectionConstants = DEFAULT_MAP_PROJECTION,
): Point2D {
    const scale = mapProjectionScale(projection);
    return {
        x: (world.x - projection.src_x1) * scale.x,
        y: (world.y - projection.src_y1) * scale.y,
    };
}

/** Inverse of {@link worldToMap}, returning Unreal centimetres. */
export function mapToWorld(
    map: Point2D,
    projection: MapProjectionConstants = DEFAULT_MAP_PROJECTION,
): Point2D {
    const scale = mapProjectionScale(projection);
    return {
        x: map.x / scale.x + projection.src_x1,
        y: map.y / scale.y + projection.src_y1,
    };
}
