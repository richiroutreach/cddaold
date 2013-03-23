#include "mapdata.h"
#include "map.h"
#include "game.h"
#include "lightmap.h"

#define INBOUNDS(x, y) (x >= -SEEX && x <= SEEX && y >= -SEEY && y <= SEEY)
#define INBOUNDS_LARGE(x, y) (x >= -LIGHTMAP_RANGE_X && x <= LIGHTMAP_RANGE_X &&\
                               y >= -LIGHTMAP_RANGE_Y && y <= LIGHTMAP_RANGE_Y)

light_map::light_map()
 : lm()
 , sm()
{
 memset(lm, 0, sizeof(lm));
 memset(sm, 0, sizeof(sm));
}

void light_map::generate(game* g, int x, int y, float natural_light, float luminance)
{
 build_light_cache(g, x, y);
 memset(lm, 0, sizeof(lm));
 memset(sm, 0, sizeof(sm));

 int dir_x[] = { 1, 0 , -1,  0 };
 int dir_y[] = { 0, 1 ,  0, -1 };
 int dir_d[] = { 180, 270, 0, 90 };

 // Daylight vision handling returned back to map due to issues it causes here
 if (natural_light > LIGHT_SOURCE_BRIGHT) {
  // Apply sunlight, first light source so just assign
  for(int sx = x - SEEX; sx <= x + SEEX; ++sx) {
   for(int sy = y - SEEY; sy <= y + SEEY; ++sy) {
    // In bright light indoor light exists to some degree
    if (!g->m.is_outside(sx, sy))
     lm[sx - x + SEEX][sy - y + SEEY] = LIGHT_AMBIENT_LOW;
   }
  }
 }

 // Apply player light sources
 if (luminance > LIGHT_AMBIENT_LOW)
  apply_light_source(g->u.posx, g->u.posy, x, y, luminance);

 for(int sx = x - LIGHTMAP_RANGE_X; sx <= x + LIGHTMAP_RANGE_X; ++sx) {
  for(int sy = y - LIGHTMAP_RANGE_Y; sy <= y + LIGHTMAP_RANGE_Y; ++sy) {
   const ter_id terrain = g->m.ter(sx, sy);
   const std::vector<item> items = g->m.i_at(sx, sy);
   const field current_field = g->m.field_at(sx, sy);
   // When underground natural_light is 0, if this changes we need to revisit
   if (natural_light > LIGHT_AMBIENT_LOW) {
    if (!g->m.is_outside(sx, sy)) {
     // Apply light sources for external/internal divide
     for(int i = 0; i < 4; ++i) {
      if (INBOUNDS_LARGE(sx - x + dir_x[i], sy - y + dir_y[i]) &&
          g->m.is_outside(sx + dir_x[i], sy + dir_y[i])) {
        if (INBOUNDS(sx - x, sy - y) && g->m.is_outside(0, 0))
        lm[sx - x + SEEX][sy - y + SEEY] = natural_light;

       if (c[sx - x + LIGHTMAP_RANGE_X][sy - y + LIGHTMAP_RANGE_Y].transparency > LIGHT_TRANSPARENCY_SOLID)
       	apply_light_arc(sx, sy, dir_d[i], x, y, natural_light);
      }
     }
    }
   }

   if (items.size() == 1 &&
       items[0].type->id == itm_flashlight_on)
    apply_light_source(sx, sy, x, y, 20);

   if(terrain == t_lava)
    apply_light_source(sx, sy, x, y, 50);

   if(terrain == t_console)
    apply_light_source(sx, sy, x, y, 3);

   if (items.size() == 1 &&
       items[0].type->id == itm_candle_lit)
    apply_light_source(sx, sy, x, y, 4);

   if(terrain == t_emergency_light)
    apply_light_source(sx, sy, x, y, 3);

   // TODO: [lightmap] Attach light brightness to fields
   switch(current_field.type) {
    case fd_fire:
     if (3 == current_field.density)
      apply_light_source(sx, sy, x, y, 160);
     else if (2 == current_field.density)
      apply_light_source(sx, sy, x, y, 60);
     else
      apply_light_source(sx, sy, x, y, 16);
     break;
    case fd_fire_vent:
    case fd_flame_burst:
     apply_light_source(sx, sy, x, y, 8);
     break;
    case fd_electricity:
     if (3 == current_field.density)
      apply_light_source(sx, sy, x, y, 8);
     else if (2 == current_field.density)
      apply_light_source(sx, sy, x, y, 1);
     else
      apply_light_source(sx, sy, x, y, LIGHT_SOURCE_LOCAL);  // kinda a hack as the square will still get marked
     break;
   }
  }
 }

 for (int i = 0; i < g->z.size(); ++i) {
  int mx = g->z[i].posx;
  int my = g->z[i].posy;
  if (INBOUNDS(mx - LIGHTMAP_RANGE_X, my - LIGHTMAP_RANGE_Y)) {
   if (g->z[i].has_effect(ME_ONFIRE)) {
    apply_light_source(mx, my, x, y, 3);
   }
   // TODO: [lightmap] Attach natural light brightness to creatures
   // TODO: [lightmap] Allow creatures to have light attacks (ie: eyebot)
   // TODO: [lightmap] Allow creatures to have facing and arc lights
   switch (g->z[i].type->id) {
    case mon_zombie_electric:
     apply_light_source(mx, my, x, y, 1);
     break;
    case mon_turret:
     apply_light_source(mx, my, x, y, 2);
     break;
    case mon_flaming_eye:
     apply_light_source(mx, my, x, y, LIGHT_SOURCE_BRIGHT);
     break;
    case mon_manhack:
     apply_light_source(mx, my, x, y, LIGHT_SOURCE_LOCAL);
     break;
   }
  }
 }

 // Apply any vehicle light sources
 VehicleList vehs = g->m.get_vehicles();
 for(int v = 0; v < vehs.size(); ++v) {
  if(vehs[v].v->lights_on) {
   int dir = vehs[v].v->face.dir();
   for (std::vector<int>::iterator part = vehs[v].v->external_parts.begin();
        part != vehs[v].v->external_parts.end(); ++part) {
    int px = vehs[v].x + vehs[v].v->parts[*part].precalc_dx[0] - LIGHTMAP_RANGE_X;
    int py = vehs[v].y + vehs[v].v->parts[*part].precalc_dy[0] - LIGHTMAP_RANGE_Y;
    if(INBOUNDS_LARGE(px, py)) {
     int dpart = vehs[v].v->part_with_feature(*part , vpf_light);
     if (dpart >= 0) {
      float luminance = vehs[v].v->part_info(dpart).power;
      if (luminance > LL_LIT) {
        apply_light_arc(px + LIGHTMAP_RANGE_X, py + LIGHTMAP_RANGE_Y, dir, x, y, luminance);
      }
     }
    }
   }
  }
 }
}

lit_level light_map::at(int dx, int dy)
{
 if (!INBOUNDS(dx, dy))
  return LL_DARK; // Out of bounds

 if (sm[dx + SEEX][dy + SEEY] >= LIGHT_SOURCE_BRIGHT)
  return LL_BRIGHT;

 if (lm[dx + SEEX][dy + SEEY] >= LIGHT_AMBIENT_LIT)
  return LL_LIT;

 if (lm[dx + SEEX][dy + SEEY] >= LIGHT_AMBIENT_LOW)
  return LL_LOW;

 return LL_DARK;
}

float light_map::ambient_at(int dx, int dy)
{
 if (!INBOUNDS(dx, dy))
  return 0.0f;

 return lm[dx + SEEX][dy + SEEY];
}

bool light_map::sees(int fx, int fy, int tx, int ty, int max_range)
{
 if(!INBOUNDS_LARGE(fx, fy) || !INBOUNDS_LARGE(tx, ty))
  return false;

 if (max_range >= 0 && (abs(tx - fx) > max_range || abs(ty - fy) > max_range))
  return false; // Out of range!

 int ax = abs(tx - fx) << 1;
 int ay = abs(ty - fy) << 1;
 int dx = (fx < tx) ? 1 : -1;
 int dy = (fy < ty) ? 1 : -1;
 int x = fx;
 int y = fy;

 // TODO: [lightmap] Pull out the common code here rather than duplication
 if (ax > ay) {
  int t = ay - (ax >> 1);
  do {
   if(t >= 0 && ((y + dy != ty) || (x + dx == tx))) {
    y += dy;
    t -= ax;
   }

   x += dx;
   t += ay;

   if(c[x + LIGHTMAP_RANGE_X][y + LIGHTMAP_RANGE_Y].transparency == LIGHT_TRANSPARENCY_SOLID)
    break;

  } while(!(x == tx && y == ty));
 } else {
  int t = ax - (ay >> 1);
  do {
   if(t >= 0 && ((x + dx != tx) || (y + dy == ty))) {
    x += dx;
    t -= ay;
   }

   y += dy;
   t += ax;

   if(c[x + LIGHTMAP_RANGE_X][y + LIGHTMAP_RANGE_Y].transparency == LIGHT_TRANSPARENCY_SOLID)
    break;

  } while(!(x == tx && y == ty));
 }

 return (x == tx && y == ty);
}

void light_map::apply_light_source(int x, int y, int cx, int cy, float luminance)
{
 bool lit[LIGHTMAP_X][LIGHTMAP_Y];
 memset(lit, 0, sizeof(lit));

 if (INBOUNDS(x - cx, y - cy)) {
  lit[x - cx + SEEX][y - cy + SEEY] = true;
  lm[x - cx + SEEX][y - cy + SEEY] += std::max(luminance, static_cast<float>(LL_LOW));
  sm[x - cx + SEEX][y - cy + SEEY] += luminance;
 }

 if (luminance > LIGHT_SOURCE_LOCAL) {
  int range = LIGHT_RANGE(luminance);
  int sx = x - cx - range; int ex = x - cx + range;
  int sy = y - cy - range; int ey = y - cy + range;

  for(int off = sx; off <= ex; ++off) {
   apply_light_ray(lit, x, y, cx + off, cy + sy, cx, cy, luminance);
   apply_light_ray(lit, x, y, cx + off, cy + ey, cx, cy, luminance);
  }

  // Skip corners with + 1 and < as they were done
  for(int off = sy + 1; off < ey; ++off) {
   apply_light_ray(lit, x, y, cx + sx, cy + off, cx, cy, luminance);
   apply_light_ray(lit, x, y, cx + ex, cy + off, cx, cy, luminance);
  }
 }
}

void light_map::apply_light_arc(int x, int y, int angle, int cx, int cy, float luminance)
{
 if (luminance <= LIGHT_SOURCE_LOCAL)
  return;

 bool lit[LIGHTMAP_X][LIGHTMAP_Y];
 memset(lit, 0, sizeof(lit));

 int range = LIGHT_RANGE(luminance);
 apply_light_source(x, y, cx, cy, LIGHT_SOURCE_LOCAL);

 // Normalise (should work with negative values too)
 angle = angle % 360;

 // East side
 if (angle < 90 || angle > 270) {
  int sy = y - cy - ((angle <  90) ? range * (( 45 - angle) / 45.0f) : range);
  int ey = y - cy + ((angle > 270) ? range * ((angle - 315) / 45.0f) : range);

  int ox = x - cx + range;
  for(int oy = sy; oy <= ey; ++oy)
   apply_light_ray(lit, x, y, cx + ox, cy + oy, cx, cy, luminance);
 }

 // South side
 if (angle < 180) {
  int sx = x - cx - ((angle < 90) ? range * (( angle - 45) / 45.0f) : range);
  int ex = x - cx + ((angle > 90) ? range * ((135 - angle) / 45.0f) : range);

  int oy = y - cy + range;
  for(int ox = sx; ox <= ex; ++ox)
   apply_light_ray(lit, x, y, cx + ox, cy + oy, cx, cy, luminance);
 }

 // West side
 if (angle > 90 && angle < 270) {
  int sy = y - cy - ((angle < 180) ? range * ((angle - 135) / 45.0f) : range);
  int ey = y - cy + ((angle > 180) ? range * ((225 - angle) / 45.0f) : range);

  int ox = x - cx - range;
  for(int oy = sy; oy <= ey; ++oy)
   apply_light_ray(lit, x, y, cx + ox, cy + oy, cx, cy, luminance);
 }

 // North side
 if (angle > 180) {
  int sx = x - cx - ((angle > 270) ? range * ((315 - angle) / 45.0f) : range);
  int ex = x - cx + ((angle < 270) ? range * ((angle - 225) / 45.0f) : range);

  int oy = y - cy - range;
  for(int ox = sx; ox <= ex; ++ox)
   apply_light_ray(lit, x, y, cx + ox, cy + oy, cx, cy, luminance);
 }
}

void light_map::apply_light_ray(bool lit[LIGHTMAP_X][LIGHTMAP_Y], int sx, int sy,
                                int ex, int ey, int cx, int cy, float luminance)
{
 int ax = abs(ex - sx) << 1;
 int ay = abs(ey - sy) << 1;
 int dx = (sx < ex) ? 1 : -1;
 int dy = (sy < ey) ? 1 : -1;
 int x = sx;
 int y = sy;

 float transparency = LIGHT_TRANSPARENCY_CLEAR;

 // TODO: [lightmap] Pull out the common code here rather than duplication
 if (ax > ay) {
  int t = ay - (ax >> 1);
  do {
   if(t >= 0) {
    y += dy;
    t -= ax;
   }

   x += dx;
   t += ay;

   if (INBOUNDS(x - cx, y - cy) && !lit[x - cx + SEEX][y - cy + SEEY]) {
    // Multiple rays will pass through the same squares so we need to record that
    lit[x - cx + SEEX][y - cy + SEEY] = true;

    // We know x is the longest angle here and squares can ignore the abs calculation
    float light = luminance / ((sx - x) * (sx - x));
    lm[x - cx + SEEX][y - cy + SEEY] += light * transparency;
   }

   if (INBOUNDS_LARGE(x - cx, y - cy))
    transparency *= c[x - cx + LIGHTMAP_RANGE_X][y - cy + LIGHTMAP_RANGE_Y].transparency;

   if (transparency <= LIGHT_TRANSPARENCY_SOLID)
    break;

  } while(!(x == ex && y == ey));
 } else {
  int t = ax - (ay >> 1);
  do {
   if(t >= 0) {
    x += dx;
    t -= ay;
   }

   y += dy;
   t += ax;

   if (INBOUNDS(x - cx, y - cy) && !lit[x - cx + SEEX][y - cy + SEEY]) {
    // Multiple rays will pass through the same squares so we need to record that
    lit[x - cx + SEEX][y - cy + SEEY] = true;

    // We know y is the longest angle here and squares can ignore the abs calculation
    float light = luminance / ((sy - y) * (sy - y));
    lm[x - cx + SEEX][y - cy + SEEY] += light;
   }

   if (INBOUNDS_LARGE(x - cx, y - cy))
    transparency *= c[x - cx + LIGHTMAP_RANGE_X][y - cy + LIGHTMAP_RANGE_Y].transparency;

   if (transparency <= LIGHT_TRANSPARENCY_SOLID)
    break;

  } while(!(x == ex && y == ey));
 }
}

// We only do this once now so we don't make 100k calls to is_outside for each
// generation. As well as close to that for the veh_at function.
void light_map::build_light_cache(game* g, int cx, int cy)
{
 // Clear cache
 for(int x = 0; x < LIGHTMAP_CACHE_X; x++) {
  for(int y = 0; y < LIGHTMAP_CACHE_Y; y++) {
   int sx = x + g->u.posx - LIGHTMAP_RANGE_X;
   int sy = y + g->u.posy - LIGHTMAP_RANGE_Y;

   c[x][y].transparency = LIGHT_TRANSPARENCY_CLEAR;
  }
 }

 for(int x = 0; x < LIGHTMAP_CACHE_X; x++) {
  for(int y = 0; y < LIGHTMAP_CACHE_Y; y++) {
   int sx = x + cx - LIGHTMAP_RANGE_X;
   int sy = y + cy - LIGHTMAP_RANGE_Y;

   if (!(terlist[g->m.ter(sx, sy)].flags & mfb(transparent))) {
    c[x][y].transparency = LIGHT_TRANSPARENCY_SOLID;
    continue;
   }

   field& f = g->m.field_at(sx, sy);
   if(f.type > 0) {
    if(!fieldlist[f.type].transparent[f.density - 1]) {
     // Fields are either transparent or not, however we want some to be translucent
     switch(f.type) {
      case fd_smoke:
      case fd_toxic_gas:
      case fd_tear_gas:
       if(f.density == 3)
        c[x][y].transparency = LIGHT_TRANSPARENCY_SOLID;
       if(f.density == 2)
        c[x][y].transparency *= 0.5;
       break;
      case fd_nuke_gas:
       c[x][y].transparency *= 0.5;
       break;
      default:
       c[x][y].transparency = LIGHT_TRANSPARENCY_SOLID;
       break;
     }
    }

    // TODO: [lightmap] Have glass reduce light as well
   }
  }
 }

 VehicleList vehs = g->m.get_vehicles();
 for(int v = 0; v < vehs.size(); ++v) {
  for (std::vector<int>::iterator part = vehs[v].v->external_parts.begin();
       part != vehs[v].v->external_parts.end(); ++part) {
   if (vehs[v].v->part_flag(*part, vpf_opaque) && vehs[v].v->parts[*part].hp > 0) {
    int dpart = vehs[v].v->part_with_feature(*part , vpf_openable);
    if (dpart < 0 || !vehs[v].v->parts[dpart].open) {
     int px = vehs[v].x + vehs[v].v->parts[*part].precalc_dx[0] - cx;
     int py = vehs[v].y + vehs[v].v->parts[*part].precalc_dy[0] - cy;
     if(INBOUNDS_LARGE(px, py))
      c[px + LIGHTMAP_RANGE_X][py + LIGHTMAP_RANGE_Y].transparency = LIGHT_TRANSPARENCY_SOLID;
    }
   }
  }
 }
}
