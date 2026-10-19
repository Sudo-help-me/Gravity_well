#include <math.h>
#include <stdio.h>

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define CENTER_X 64.0f
#define CENTER_Y 32.0f
#define HOLE_RADIUS 2.0f
#define GRAVITY_CONSTANT 0.006f
#define BLACK_HOLE_MASS 1000.0f
#define MAX_BODIES 12
#define TRAIL_LENGTH 24
#define TRAIL_INTERVAL 3
#define CURSOR_STEP 4
#define TICK_MS 16

typedef struct {
    float x;
    float y;
    float vx;
    float vy;
    float radius;
    uint8_t type;
    uint8_t trail_x[TRAIL_LENGTH];
    uint8_t trail_y[TRAIL_LENGTH];
    uint8_t trail_count;
    uint8_t trail_next;
    uint8_t trail_tick;
} Body;

typedef struct {
    Body bodies[MAX_BODIES];
    uint8_t body_count;
    uint8_t cursor_x;
    uint8_t cursor_y;
    uint8_t selected_type;
    float score;
    bool game_over;
    bool paused;
    bool trace_enabled;
    bool dark_mode;
} Game;

typedef struct {
    Game game;
    FuriMessageQueue* input_queue;
    FuriMutex* game_mutex;
} App;

static const float body_masses[] = {1.0f, 100.0f, 500.0f};
static const float body_radii[] = {1.0f, 1.0f, 2.0f};
static const float body_score_weights[] = {1.0f, 10.0f, 20.0f};
static const char* body_names[] = {"MOON", "PLANET", "STAR"};

static void game_reset(Game* game) {
    game->body_count = 1;
    game->score = 0;
    game->game_over = false;
    game->paused = false;
    game->trace_enabled = true;
    game->dark_mode = false;
    game->selected_type = 1;
    game->cursor_x = SCREEN_WIDTH / 2 + 22;
    game->cursor_y = SCREEN_HEIGHT / 2;

    Body* first_body = &game->bodies[0];
    *first_body = (Body){0};
    first_body->x = SCREEN_WIDTH / 2.7f;
    first_body->y = SCREEN_HEIGHT / 2.7f;
    first_body->vy = 0.50f;
    first_body->radius = body_radii[1];
    first_body->type = 1;
}

static void game_add_body(Game* game) {
    if(game->game_over || game->body_count >= MAX_BODIES) return;

    Body* body = &game->bodies[game->body_count++];
    *body = (Body){0};
    body->x = (float)game->cursor_x;
    body->y = (float)game->cursor_y;
    body->vy = 0.50f;
    body->radius = body_radii[game->selected_type];
    body->type = game->selected_type;
}

static bool segment_touches_hole(float x0, float y0, float x1, float y1) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float length_squared = dx * dx + dy * dy;
    float projection = 0.0f;
    if(length_squared > 0.0f) {
        projection = ((CENTER_X - x0) * dx + (CENTER_Y - y0) * dy) /
                     length_squared;
        if(projection < 0.0f) projection = 0.0f;
        if(projection > 1.0f) projection = 1.0f;
    }

    const float nearest_x = x0 + projection * dx - CENTER_X;
    const float nearest_y = y0 + projection * dy - CENTER_Y;
    return nearest_x * nearest_x + nearest_y * nearest_y <= HOLE_RADIUS * HOLE_RADIUS;
}

static void game_tick(Game* game) {
    float old_x[MAX_BODIES];
    float old_y[MAX_BODIES];
    float acceleration_x[MAX_BODIES];
    float acceleration_y[MAX_BODIES];
    const float step = 0.25f;

    for(uint8_t substep = 0; substep < 4; substep++) {
        for(uint8_t i = 0; i < game->body_count; i++) {
            Body* body = &game->bodies[i];
            old_x[i] = body->x;
            old_y[i] = body->y;

            const float dx = CENTER_X - body->x;
            const float dy = CENTER_Y - body->y;
            const float distance_squared = dx * dx + dy * dy;
            if(distance_squared <= HOLE_RADIUS * HOLE_RADIUS) {
                game->game_over = true;
                return;
            }

            const float distance = sqrtf(distance_squared);
            const float gravity =
                GRAVITY_CONSTANT * BLACK_HOLE_MASS / (distance_squared * distance);
            acceleration_x[i] = dx * gravity;
            acceleration_y[i] = dy * gravity;

            for(uint8_t j = 0; j < game->body_count; j++) {
                if(i == j || body_masses[j] < body_masses[i]) continue;

                const float other_dx = game->bodies[j].x - body->x;
                const float other_dy = game->bodies[j].y - body->y;
                const float other_distance_squared =
                    other_dx * other_dx + other_dy * other_dy;
                if(other_distance_squared < 1.0f) continue;

                const float other_distance = sqrtf(other_distance_squared);
                const float pull = GRAVITY_CONSTANT * body_masses[j] /
                                   (other_distance_squared * other_distance);
                acceleration_x[i] += other_dx * pull;
                acceleration_y[i] += other_dy * pull;
            }
        }

        for(uint8_t i = 0; i < game->body_count; i++) {
            Body* body = &game->bodies[i];
            body->x += body->vx * step;
            body->y += body->vy * step;
            body->vx += acceleration_x[i] * step;
            body->vy += acceleration_y[i] * step;

            if(segment_touches_hole(old_x[i], old_y[i], body->x, body->y)) {
                game->game_over = true;
                return;
            }
        }

        for(uint8_t i = 0; i < game->body_count; i++) {
            for(uint8_t j = i + 1; j < game->body_count; j++) {
                const float dx = game->bodies[i].x - game->bodies[j].x;
                const float dy = game->bodies[i].y - game->bodies[j].y;
                const float collision_radius =
                    game->bodies[i].radius + game->bodies[j].radius;
                if(dx * dx + dy * dy <= collision_radius * collision_radius) {
                    game->game_over = true;
                    return;
                }
            }
        }
    }

    for(uint8_t i = 0; i < game->body_count; i++) {
        Body* body = &game->bodies[i];
        const float dx = CENTER_X - body->x;
        const float dy = CENTER_Y - body->y;
        const float distance = sqrtf(dx * dx + dy * dy);
        if(game->body_count > 1) {
            game->score += body_score_weights[body->type] / (distance * 6.0f);
        }

        body->trail_tick++;
        if(body->trail_tick >= TRAIL_INTERVAL) {
            body->trail_tick = 0;
            if(body->x >= 0.0f && body->x < SCREEN_WIDTH &&
               body->y >= 0.0f && body->y < SCREEN_HEIGHT) {
                body->trail_x[body->trail_next] = (uint8_t)body->x;
                body->trail_y[body->trail_next] = (uint8_t)body->y;
                body->trail_next = (body->trail_next + 1) % TRAIL_LENGTH;
                if(body->trail_count < TRAIL_LENGTH) body->trail_count++;
            }
        }
    }
}

static Color game_ink(const Game* game) {
    return game->dark_mode ? ColorWhite : ColorBlack;
}

static Color game_paper(const Game* game) {
    return game->dark_mode ? ColorBlack : ColorWhite;
}

static void draw_hole(Canvas* canvas, const Game* game) {
    if(game->dark_mode) {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_disc(canvas, (uint8_t)CENTER_X, (uint8_t)CENTER_Y, 3);
    }
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_disc(canvas, (uint8_t)CENTER_X, (uint8_t)CENTER_Y, 2);
}

static void draw_body_trace(Canvas* canvas, const Game* game, const Body* body) {
    if(!game->trace_enabled || body->trail_count < 2) return;

    canvas_set_color(canvas, game_ink(game));
    uint8_t index =
        (body->trail_next + TRAIL_LENGTH - body->trail_count) % TRAIL_LENGTH;
    for(uint8_t i = 1; i < body->trail_count; i++) {
        const uint8_t next = (index + 1) % TRAIL_LENGTH;
        canvas_draw_line(
            canvas,
            body->trail_x[index],
            body->trail_y[index],
            body->trail_x[next],
            body->trail_y[next]);
        index = next;
    }
}

static void draw_body(Canvas* canvas, const Game* game, const Body* body) {
    if(body->x < 0.0f || body->x >= SCREEN_WIDTH ||
       body->y < 0.0f || body->y >= SCREEN_HEIGHT) {
        return;
    }

    const uint8_t x = (uint8_t)body->x;
    const uint8_t y = (uint8_t)body->y;
    const uint8_t radius = (uint8_t)body->radius;

    canvas_set_color(canvas, game_ink(game));
    if(body->type == 0) {
        canvas_draw_circle(canvas, x, y, radius);
    } else {
        canvas_draw_disc(canvas, x, y, radius);
        if(body->type == 1) {
            canvas_set_color(canvas, game_paper(game));
            canvas_draw_pixel(canvas, x, y);
        } else {
            canvas_set_color(canvas, game_paper(game));
            const uint8_t left = x > radius ? x - radius : 0;
            const uint8_t right =
                x + radius < SCREEN_WIDTH ? x + radius : SCREEN_WIDTH - 1;
            const uint8_t top = y > radius ? y - radius : 0;
            const uint8_t bottom =
                y + radius < SCREEN_HEIGHT ? y + radius : SCREEN_HEIGHT - 1;
            canvas_draw_line(
                canvas,
                left,
                y,
                right,
                y);
            canvas_draw_line(
                canvas,
                x,
                top,
                x,
                bottom);
        }
    }
}

static void draw_placement_cursor(Canvas* canvas, const Game* game) {
    const uint8_t radius = (uint8_t)body_radii[game->selected_type] + 2;
    canvas_set_color(canvas, game_ink(game));
    canvas_draw_circle(canvas, game->cursor_x, game->cursor_y, radius);
    canvas_draw_line(
        canvas,
        game->cursor_x > 2 ? game->cursor_x - 2 : 0,
        game->cursor_y,
        game->cursor_x + 2,
        game->cursor_y);
    canvas_draw_line(
        canvas,
        game->cursor_x,
        game->cursor_y > 2 ? game->cursor_y - 2 : 0,
        game->cursor_x,
        game->cursor_y + 2);
}

static void draw_callback(Canvas* canvas, void* context) {
    App* app = context;
    furi_check(furi_mutex_acquire(app->game_mutex, FuriWaitForever) == FuriStatusOk);
    const Game* game = &app->game;

    if(game->dark_mode) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    } else {
        canvas_clear(canvas);
    }

    for(uint8_t i = 0; i < game->body_count; i++) {
        draw_body_trace(canvas, game, &game->bodies[i]);
    }

    draw_hole(canvas, game);
    for(uint8_t i = 0; i < game->body_count; i++) {
        draw_body(canvas, game, &game->bodies[i]);
    }

    if(!game->game_over) draw_placement_cursor(canvas, game);

    canvas_set_color(canvas, game_ink(game));
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 8, "THE BLACK HOLE");
    canvas_draw_str(canvas, 2, 17, body_names[game->selected_type]);
    if(game->body_count == MAX_BODIES && !game->game_over) {
        canvas_draw_str(canvas, 42, 17, "FULL");
    }

    char score_text[16];
    (void)snprintf(
        score_text,
        sizeof(score_text),
        "SCORE %lu",
        (unsigned long)game->score);
    canvas_draw_str(canvas, 82, 8, score_text);

    if(game->paused && !game->game_over) {
        canvas_draw_box(canvas, 31, 24, 66, 15);
        canvas_set_color(canvas, game_paper(game));
        canvas_draw_str(canvas, 46, 34, "PAUSED");
    } else if(game->game_over) {
        canvas_set_color(canvas, game_ink(game));
        canvas_draw_box(canvas, 25, 23, 78, 22);
        canvas_set_color(canvas, game_paper(game));
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 43, 33, "GAME OVER");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 37, 42, "OK: RESTART");
    } else {
        canvas_set_color(canvas, game_ink(game));
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 57, "LR:X UD:Y HOLD UP:BODY");
        canvas_draw_str(canvas, 2, 63, "OK:DROP HOLD OK:PAUSE");
        canvas_draw_str(canvas, 96, 17, game->trace_enabled ? "T+" : "T-");
        canvas_draw_str(canvas, 112, 17, game->dark_mode ? "D+" : "D-");
    }
    furi_check(furi_mutex_release(app->game_mutex) == FuriStatusOk);
}

static void input_callback(InputEvent* event, void* context) {
    FuriMessageQueue* queue = context;
    furi_check(furi_message_queue_put(queue, event, FuriWaitForever) == FuriStatusOk);
}

static bool handle_input(Game* game, const InputEvent* event) {
    if(event->type == InputTypeLong && !game->game_over) {
        switch(event->key) {
        case InputKeyOk:
            game->paused = !game->paused;
            break;
        case InputKeyUp:
            game->selected_type = (game->selected_type + 1) % 3;
            break;
        case InputKeyLeft:
            game->trace_enabled = !game->trace_enabled;
            if(game->trace_enabled) {
                for(uint8_t i = 0; i < game->body_count; i++) {
                    game->bodies[i].trail_count = 0;
                    game->bodies[i].trail_next = 0;
                }
            }
            break;
        case InputKeyRight:
            game->dark_mode = !game->dark_mode;
            break;
        default:
            break;
        }
        return true;
    }

    if(event->type != InputTypeShort) return true;
    if(event->key == InputKeyBack) return false;

    if(game->game_over) {
        if(event->key == InputKeyOk) game_reset(game);
        return true;
    }

    switch(event->key) {
    case InputKeyLeft:
        game->cursor_x = game->cursor_x > 3 + CURSOR_STEP ?
                             game->cursor_x - CURSOR_STEP :
                             3;
        break;
    case InputKeyRight:
        game->cursor_x = game->cursor_x < SCREEN_WIDTH - 4 - CURSOR_STEP ?
                             game->cursor_x + CURSOR_STEP :
                             SCREEN_WIDTH - 4;
        break;
    case InputKeyUp:
        game->cursor_y = game->cursor_y > 19 + CURSOR_STEP ?
                             game->cursor_y - CURSOR_STEP :
                             19;
        break;
    case InputKeyDown:
        game->cursor_y = game->cursor_y < SCREEN_HEIGHT - 11 - CURSOR_STEP ?
                             game->cursor_y + CURSOR_STEP :
                             SCREEN_HEIGHT - 11;
        break;
    case InputKeyOk:
        game_add_body(game);
        break;
    default:
        break;
    }

    return true;
}

int32_t gravity_well_app(void* p) {
    UNUSED(p);

    App app = {0};
    game_reset(&app.game);
    app.input_queue = furi_message_queue_alloc(16, sizeof(InputEvent));
    furi_check(app.input_queue);
    app.game_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    furi_check(app.game_mutex);

    ViewPort* view_port = view_port_alloc();
    furi_check(view_port);
    view_port_draw_callback_set(view_port, draw_callback, &app);
    view_port_input_callback_set(view_port, input_callback, app.input_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    bool running = true;
    while(running) {
        InputEvent event;
        const FuriStatus status =
            furi_message_queue_get(app.input_queue, &event, TICK_MS);
        furi_check(furi_mutex_acquire(app.game_mutex, FuriWaitForever) == FuriStatusOk);
        if(status == FuriStatusOk) {
            running = handle_input(&app.game, &event);
        } else {
            furi_check(status == FuriStatusErrorTimeout);
        }

        if(running && !app.game.paused && !app.game.game_over &&
           app.game.body_count > 0) {
            game_tick(&app.game);
        }
        furi_check(furi_mutex_release(app.game_mutex) == FuriStatusOk);
        view_port_update(view_port);
    }

    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_mutex_free(app.game_mutex);
    furi_message_queue_free(app.input_queue);
    return 0;
}
