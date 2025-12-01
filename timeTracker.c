#define _GNU_SOURCE
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define MAX_ENTRIES 1000
#define MAX_NAME    64
#define MAX_INPUT   128
#define EDGE_GRAB   20

typedef struct { char name[MAX_NAME]; time_t start, end; double duration_years; Color color; } Entry;
typedef struct { Entry entries[MAX_ENTRIES]; int count; time_t view_start; double pixels_per_year; } Tracker;

typedef struct {
    char text[MAX_INPUT];
    int cursor_pos;
    Rectangle rect;
    bool active;
} TextInput;

void InitTextInput(TextInput *ti, Rectangle r, const char *initial) {
    ti->rect = r; ti->active = false; ti->cursor_pos = 0;
    strncpy(ti->text, initial ? initial : "", MAX_INPUT-1); ti->text[MAX_INPUT-1] = '\0';
}

static float TextWidthUpTo(Font font, const char *text, int char_count, float fontSize, float spacing) {
    if (char_count <= 0) return 0.0f;
    char temp[MAX_INPUT]; int len = fmin(char_count, (int)strlen(text));
    strncpy(temp, text, len); temp[len] = '\0';
    return MeasureTextEx(font, temp, fontSize, spacing).x;
}

void DrawTextInput(TextInput *ti, Font font) {
    Color bg = ti->active ? (Color){50,80,140,255} : (Color){45,45,55,255};
    DrawRectangleRec(ti->rect, bg);
    DrawRectangleLinesEx(ti->rect, 2, ti->active ? SKYBLUE : GRAY);
    const char *display = ti->text[0] ? ti->text : "(empty)";
    DrawTextEx(font, display, (Vector2){ti->rect.x + 10, ti->rect.y + 10}, 20, 1, WHITE);
    if (ti->active && ((int)(GetTime() * 2) % 2 == 0)) {
        float x = ti->rect.x + 10 + TextWidthUpTo(font, ti->text, ti->cursor_pos, 20, 1);
        DrawRectangle((int)x, (int)ti->rect.y + 10, 2, 20, WHITE);
    }
}

void UpdateTextInput(TextInput *ti, Font font) {
    Vector2 mouse = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        ti->active = CheckCollisionPointRec(mouse, ti->rect);
        if (ti->active) {
            float rel_x = mouse.x - (ti->rect.x + 10);
            if (rel_x < 0) ti->cursor_pos = 0;
            else {
                int low = 0, high = strlen(ti->text);
                while (low < high) {
                    int mid = (low + high + 1) / 2;
                    if (TextWidthUpTo(font, ti->text, mid, 20, 1) <= rel_x) low = mid;
                    else high = mid - 1;
                }
                ti->cursor_pos = low;
            }
        }
    }
    if (!ti->active) return;

    int key = GetCharPressed();
    while (key > 0) {
        if (key >= 32 && key <= 125 && strlen(ti->text) < MAX_INPUT-2) {
            memmove(ti->text + ti->cursor_pos + 1, ti->text + ti->cursor_pos,
                    strlen(ti->text + ti->cursor_pos) + 1);
            ti->text[ti->cursor_pos++] = (char)key;
        }
        key = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) && ti->cursor_pos > 0) { memmove(ti->text + ti->cursor_pos - 1, ti->text + ti->cursor_pos, strlen(ti->text + ti->cursor_pos) + 1); ti->cursor_pos--; }
    if (IsKeyPressed(KEY_DELETE) && ti->cursor_pos < (int)strlen(ti->text)) { memmove(ti->text + ti->cursor_pos, ti->text + ti->cursor_pos + 1, strlen(ti->text + ti->cursor_pos + 1) + 1); }
    if (IsKeyPressed(KEY_LEFT) && ti->cursor_pos > 0) ti->cursor_pos--;
    if (IsKeyPressed(KEY_RIGHT) && ti->cursor_pos < (int)strlen(ti->text)) ti->cursor_pos++;
    if (IsKeyPressed(KEY_HOME)) ti->cursor_pos = 0;
    if (IsKeyPressed(KEY_END)) ti->cursor_pos = strlen(ti->text);
}

time_t ParseDateTime(const char *s) {
    struct tm tm = {0};
    strptime(s, "%Y-%m-%d %H:%M", &tm) || strptime(s, "%Y-%m-%d", &tm);
    return mktime(&tm);
}

void Save(Tracker *t, const char *file) {
    FILE *f = fopen(file, "w"); if (!f) return;
    fprintf(f, "[\n");
    for (int i = 0; i < t->count; i++) {
        char s1[64], s2[64];
        strftime(s1, sizeof(s1), "%Y-%m-%d %H:%M", localtime(&t->entries[i].start));
        strftime(s2, sizeof(s2), "%Y-%m-%d %H:%M", localtime(&t->entries[i].end));
        fprintf(f, "  {\"name\":\"%s\",\"start\":\"%s\",\"end\":\"%s\"}%s\n",
                t->entries[i].name, s1, s2, i < t->count-1 ? "," : "");
    }
    fprintf(f, "]\n"); fclose(f);
}

void Load(Tracker *t, const char *file) {
    FILE *f = fopen(file, "r"); if (!f) return;
    char line[512]; t->count = 0;
    while (fgets(line, sizeof(line), f) && t->count < MAX_ENTRIES) {
        char name[MAX_NAME] = {0}, start[64] = {0}, end[64] = {0};
        if (sscanf(line, "  {\"name\":\"%63[^\"]\",\"start\":\"%63[^\"]\",\"end\":\"%63[^\"]\"}", name, start, end) == 3) {
            strncpy(t->entries[t->count].name, name, MAX_NAME-1);
            t->entries[t->count].start = ParseDateTime(start);
            t->entries[t->count].end   = ParseDateTime(end);
            t->entries[t->count].duration_years = difftime(t->entries[t->count].end, t->entries[t->count].start) / (365.25*86400);
            t->entries[t->count].color = (Color){GetRandomValue(90,230), GetRandomValue(90,230), GetRandomValue(110,240), 255};
            t->count++;
        }
    }
    fclose(f);
}

int main(void) {
    const int W = 1500, H = 900;
    InitWindow(W, H, "Lifetime Visual Time Tracker");
    SetTargetFPS(60);

    Font font = LoadFontEx("/usr/share/fonts/TTF/DejaVuSans.ttf", 22, NULL, 0);
    if (font.texture.id == 0) font = GetFontDefault();

    Tracker tracker = {0};
    tracker.pixels_per_year = 700.0;
    tracker.view_start = time(NULL) - 20LL * 365 * 86400;
    Load(&tracker, "timetracker.json");

    TextInput name   = {0}; InitTextInput(&name,   (Rectangle){180, 30, 420, 44}, "My Life");
    TextInput startf = {0}; InitTextInput(&startf, (Rectangle){680, 30, 200, 44}, "1990-01-01");
    TextInput endf   = {0}; InitTextInput(&endf,   (Rectangle){960, 30, 200, 44}, "2030-12-31");

    int selected = -1;
    int dragging = -1;        // event being dragged
    int drag_mode = 0;        // 0=move, 1=left edge, 2=right edge
    time_t drag_offset = 0;

    while (!WindowShouldClose()) {
        Vector2 mouse = GetMousePosition();
        double secs_per_pixel = (365.25 * 86400) / tracker.pixels_per_year;

        // RIGHT-CLICK PANNING — restored and rock-solid
static Vector2 last_mouse = {0};
if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
    Vector2 mouse = GetMousePosition();
    if (last_mouse.x != 0 || last_mouse.y != 0) {
        Vector2 delta = (Vector2){mouse.x - last_mouse.x, mouse.y - last_mouse.y};
        tracker.view_start -= (time_t)(delta.x * secs_per_pixel);
    }
    last_mouse = mouse;
    HideCursor();
} else {
    last_mouse = (Vector2){0};
    ShowCursor();
}

        // Zoom
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            double old = tracker.pixels_per_year;
            tracker.pixels_per_year *= (wheel > 0 ? 1.6 : 0.625);
            tracker.pixels_per_year = fmax(20.0, fmin(tracker.pixels_per_year, 500000.0));
            double world_x = tracker.view_start + (mouse.x - 100) / old * 365.25*86400;
            tracker.view_start = (time_t)(world_x - (mouse.x - 100) / tracker.pixels_per_year * 365.25*86400);
        }

        // Start dragging event
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && dragging == -1) {
            for (int i = 0; i < tracker.count; i++) {
                Entry *e = &tracker.entries[i];
                double x = 100 + difftime(e->start, tracker.view_start) / (365.25*86400) * tracker.pixels_per_year;
                double w = e->duration_years * tracker.pixels_per_year;
                if (w < 4) w = 4;
                Rectangle r = {x, 120 + i*72, w, 50};

                if (CheckCollisionPointRec(mouse, r)) {
                    selected = dragging = i;
                    double rel_x = mouse.x - x;
                    if (rel_x < EDGE_GRAB) drag_mode = 1;
                    else if (rel_x > w - EDGE_GRAB) drag_mode = 2;
                    else drag_mode = 0;

                    time_t cursor_time = tracker.view_start + (time_t)((mouse.x - 100) / tracker.pixels_per_year * 365.25*86400);
                    if (drag_mode == 1) drag_offset = e->start - cursor_time;
                    else if (drag_mode == 2) drag_offset = e->end - cursor_time;
                    else drag_offset = e->start - cursor_time;
                    break;
                }
            }
        }

        // Update dragging
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && dragging != -1) {
            time_t cursor_time = tracker.view_start + (time_t)((mouse.x - 100) / tracker.pixels_per_year * 365.25*86400);
            Entry *e = &tracker.entries[dragging];

            if (drag_mode == 0) {
                time_t duration = e->end - e->start;
                e->start = cursor_time + drag_offset;
                e->end = e->start + duration;
            } else if (drag_mode == 1) {
                time_t new_start = cursor_time + drag_offset;
                if (new_start < e->end - 86400) e->start = new_start;
            } else if (drag_mode == 2) {
                time_t new_end = cursor_time + drag_offset;
                if (new_end > e->start + 86400) e->end = new_end;
            }
            e->duration_years = difftime(e->end, e->start) / (365.25*86400);
        }

        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) dragging = -1;

        UpdateTextInput(&name, font);
        UpdateTextInput(&startf, font);
        UpdateTextInput(&endf, font);

        if (IsKeyPressed(KEY_ENTER) && strlen(name.text) && strlen(startf.text) && strlen(endf.text)) {
            time_t s = ParseDateTime(startf.text);
            time_t e = ParseDateTime(endf.text);
            if (s && e > s && tracker.count < MAX_ENTRIES) {
                Entry *en = &tracker.entries[tracker.count++];
                strncpy(en->name, name.text, MAX_NAME-1);
                en->start = s; en->end = e;
                en->duration_years = difftime(e, s) / (365.25*86400);
                en->color = (Color){GetRandomValue(90,230), GetRandomValue(90,230), GetRandomValue(110,240), 255};
            }
        }

        if (IsKeyPressed(KEY_DELETE) && selected >= 0) {
            memmove(&tracker.entries[selected], &tracker.entries[selected+1], sizeof(Entry)*(tracker.count - selected - 1));
            tracker.count--; selected = -1;
        }

        BeginDrawing();
        ClearBackground((Color){15,15,30,255});

        // Draw events
        for (int i = 0; i < tracker.count; i++) {
            Entry *e = &tracker.entries[i];
            double x = 100 + difftime(e->start, tracker.view_start) / (365.25*86400) * tracker.pixels_per_year;
            double w = e->duration_years * tracker.pixels_per_year;
            if (w < 4) w = 4;
            float y = 120 + i*72;
            Rectangle r = {x, y, w, 50};

            bool hover = CheckCollisionPointRec(mouse, r);
            Color col = (dragging == i) ? Fade(e->color, 1.3f) : (hover ? Fade(e->color, 0.9f) : e->color);
            DrawRectangleRec(r, col);
            if (selected == i) DrawRectangleLinesEx(r, 4, YELLOW);

            if (hover || dragging == i) {
                DrawRectangle(x, y, EDGE_GRAB, 50, Fade(WHITE, 0.2f));
                DrawRectangle(x + w - EDGE_GRAB, y, EDGE_GRAB, 50, Fade(WHITE, 0.2f));
            }

            DrawTextEx(font, e->name, (Vector2){x + 14, y + 12}, 22, 1, WHITE);
            char buf[64]; snprintf(buf, 64, "%.2f years", e->duration_years);
            DrawTextEx(font, buf, (Vector2){x + w - MeasureTextEx(font, buf, 20, 1).x - 14, y + 14}, 20, 1, WHITE);
        }

        // Smart minimal ticks
        DrawLine(100, 94, W-50, 94, (Color){70,70,90,255});
        double pixels_per_day = 86400.0 / secs_per_pixel;
        time_t interval; const char* fmt; int tick_len = 3;

        if (pixels_per_day > 80) { interval = 86400; fmt = "%d"; tick_len = 5; }
        else if (pixels_per_day > 20) { interval = 7*86400; fmt = "%d"; tick_len = 4; }
        else if (pixels_per_day * 30.4 > 60) { interval = 30.4*86400; fmt = "%b"; tick_len = 4; }
        else { interval = 365.25*86400; fmt = "%Y"; tick_len = 3; }

        time_t t0 = tracker.view_start - (tracker.view_start % interval);
        if (t0 < tracker.view_start) t0 += interval;

        for (time_t t = t0; t < tracker.view_start + (W/tracker.pixels_per_year)*365.25*86400 + interval; t += interval) {
            float x = 100 + difftime(t, tracker.view_start) / secs_per_pixel;
            if (x < 80 || x > W-30) continue;
            DrawLineEx((Vector2){x, 94 - tick_len}, (Vector2){x, 100}, tick_len == 5 ? 5 : 3, WHITE);
            char label[16]; struct tm *tm = localtime(&t);
            strftime(label, sizeof(label), fmt, tm);
            DrawTextPro(font, label, (Vector2){x + 6, 85}, (Vector2){0,0}, 90.0f, 18, 1, LIGHTGRAY);
        }

        DrawTextEx(font, "Name:", (Vector2){100, 38}, 22, 1, WHITE);
        DrawTextEx(font, "Start:", (Vector2){600, 38}, 22, 1, WHITE);
        DrawTextEx(font, "End:", (Vector2){890, 38}, 22, 1, WHITE);
        DrawTextInput(&name, font);
        DrawTextInput(&startf, font);
        DrawTextInput(&endf, font);

        DrawTextEx(font, "Left-drag: move/resize • Right-drag: pan • Scroll: zoom • ENTER add • DEL remove",
                   (Vector2){10, H-38}, 19, 1, (Color){190,210,230,255});

        EndDrawing();
    }

    Save(&tracker, "timetracker.json");
    UnloadFont(font);
    CloseWindow();
    return 0;
}
