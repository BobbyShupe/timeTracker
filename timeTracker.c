#define _GNU_SOURCE
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>          // ← THIS WAS MISSING

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

// ─────────────────────────────────────────────────────────────────────────────
// Global state
// ─────────────────────────────────────────────────────────────────────────────
static Font font;
static Tracker tracker = {0};
static TextInput name_input, start_input, end_input;
static int selected = -1;
static int dragging = -1;
static int drag_mode = 0;
static time_t drag_offset = 0;
static int last_selected = -1;

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations (so functions can be in any order)
// ─────────────────────────────────────────────────────────────────────────────
void DrawTextInput(TextInput *ti, Font font);
void UpdateTextInput(TextInput *ti, Font font);

// ─────────────────────────────────────────────────────────────────────────────
// Helper Functions
// ─────────────────────────────────────────────────────────────────────────────
void InitTextInput(TextInput *ti, Rectangle r, const char *initial) {
    ti->rect = r; ti->active = false; ti->cursor_pos = 0;
    strncpy(ti->text, initial ? initial : "", MAX_INPUT-1);
    ti->text[MAX_INPUT-1] = '\0';
}

time_t ParseDateTime(const char *s) {
    struct tm tm = {0};
    if (strptime(s, "%Y-%m-%d %H:%M", &tm) || strptime(s, "%Y-%m-%d", &tm))
        return mktime(&tm);
    return 0;
}

void SaveTracker(const char *file) {
    FILE *f = fopen(file, "w"); if (!f) return;
    fprintf(f, "[\n");
    for (int i = 0; i < tracker.count; i++) {
        char s1[64], s2[64];
        strftime(s1, sizeof(s1), "%Y-%m-%d %H:%M", localtime(&tracker.entries[i].start));
        strftime(s2, sizeof(s2), "%Y-%m-%d %H:%M", localtime(&tracker.entries[i].end));
        fprintf(f, "  {\"name\":\"%s\",\"start\":\"%s\",\"end\":\"%s\"}%s\n",
                tracker.entries[i].name, s1, s2, i < tracker.count-1 ? "," : "");
    }
    fprintf(f, "]\n"); fclose(f);
}

void LoadTracker(const char *file) {
    FILE *f = fopen(file, "r"); if (!f) return;
    char line[512]; tracker.count = 0;
    while (fgets(line, sizeof(line), f) && tracker.count < MAX_ENTRIES) {
        char name[MAX_NAME] = {0}, start[64] = {0}, end[64] = {0};
        if (sscanf(line, "  {\"name\":\"%63[^\"]\",\"start\":\"%63[^\"]\",\"end\":\"%63[^\"]\"}", name, start, end) == 3) {
            time_t s = ParseDateTime(start);
            time_t e = ParseDateTime(end);
            if (s && e > s) {
                Entry *en = &tracker.entries[tracker.count++];
                strncpy(en->name, name, MAX_NAME-1);
                en->start = s; en->end = e;
                en->duration_years = difftime(e, s) / (365.25*86400);
                en->color = (Color){GetRandomValue(90,230), GetRandomValue(90,230), GetRandomValue(110,240), 255};
            }
        }
    }
    fclose(f);
}

void SyncInputsToSelected(void) {
    if (selected < 0 || selected >= tracker.count) return;
    Entry *e = &tracker.entries[selected];
    strncpy(name_input.text, e->name, MAX_INPUT-1); name_input.text[MAX_INPUT-1] = '\0';
    strftime(start_input.text, MAX_INPUT, "%Y-%m-%d", localtime(&e->start));
    strftime(end_input.text,   MAX_INPUT, "%Y-%m-%d", localtime(&e->end));
}

void ApplyInputsToSelected(void) {
    if (selected < 0 || selected >= tracker.count) return;
    Entry *e = &tracker.entries[selected];
    time_t s = ParseDateTime(start_input.text);
    time_t e_time = ParseDateTime(end_input.text);  // ← Renamed to avoid conflict
    if (s && e_time > s) {
        strncpy(e->name, name_input.text[0] ? name_input.text : "Untitled", MAX_NAME-1);
        e->start = s;
        e->end = e_time;                            // ← Fixed: was "e = e"
        e->duration_years = difftime(e->end, e->start) / (365.25*86400);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Input Handling
// ─────────────────────────────────────────────────────────────────────────────
void HandlePanningAndZooming(void)
{
    Vector2 mouse = GetMousePosition();
    const float left = 100.0f;

    double secs_per_pixel = (365.25 * 86400.0) / tracker.pixels_per_year;

    // ───── SMOOTH PANNING (Right drag) ─────
    static Vector2 last_mouse = {0};
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON))
    {
        if (last_mouse.x != 0.0f)
        {
            float delta_x = mouse.x - last_mouse.x;
            tracker.view_start -= (time_t)(delta_x * secs_per_pixel);
        }
        last_mouse = mouse;
        HideCursor();
    }
    else
    {
        last_mouse = (Vector2){0};
        ShowCursor();
    }

    // ───── PRECISE, DIRECT, NO-GLIDE ZOOM (exactly what you asked for) ─────
    float wheel = GetMouseWheelMoveV().y;
    if (wheel == 0.0f) wheel = GetMouseWheelMove();  // fallback

    if (wheel != 0.0f)
    {
        // Time under mouse cursor — stays perfectly fixed
        time_t time_under_mouse = tracker.view_start + (time_t)((mouse.x - left) * secs_per_pixel);

        // Precise, predictable zoom steps
        double zoom_factor = (wheel > 0) ? 1.25 : 0.80;   // 25% in / 20% out → feels perfect

        tracker.pixels_per_year *= zoom_factor;

        // Hard, safe limits
        if (tracker.pixels_per_year < 20.0)     tracker.pixels_per_year = 20.0;
        if (tracker.pixels_per_year > 2000000.0) tracker.pixels_per_year = 2000000.0;

        // Re-center exactly under mouse
        secs_per_pixel = (365.25 * 86400.0) / tracker.pixels_per_year;
        tracker.view_start = time_under_mouse - (time_t)((mouse.x - left) * secs_per_pixel);
    }
}

void HandleSelectionAndDragging(void) {
    Vector2 mouse = GetMousePosition();
    double secs_per_pixel = (365.25 * 86400.0) / tracker.pixels_per_year;

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && dragging == -1) {
        for (int i = 0; i < tracker.count; i++) {
            Entry *e = &tracker.entries[i];

            double secs_from_view = difftime(e->start, tracker.view_start);
            float x = 100.0f + (float)(secs_from_view / (365.25*86400.0) * tracker.pixels_per_year);
            float w = (float)(e->duration_years * tracker.pixels_per_year);
            if (w < 4.0f) w = 4.0f;

			const float start_y = 300.0f;
			const float row_height = 36.0f;
			float y = start_y + i * row_height;                     // ← SAME AS DrawEvents() !!
            Rectangle rec = { x, y, w, 28.0f };

            if (CheckCollisionPointRec(mouse, rec)) {
                selected = i;
                dragging = i;

                float rel = mouse.x - x;
                drag_mode = (rel < EDGE_GRAB) ? 1 :            // left edge
                            (rel > w - EDGE_GRAB) ? 2 : 0;   // right edge or middle

                time_t cursor_time = tracker.view_start + (time_t)((mouse.x - 100.0f) * secs_per_pixel);
                drag_offset = (drag_mode == 1) ? e->start - cursor_time :
                              (drag_mode == 2) ? e->end   - cursor_time :
                                                  e->start - cursor_time;
                return;   // important — don’t check other events
            }
        }
        // clicked empty space → deselect (unless on text inputs)
        if (!CheckCollisionPointRec(mouse, name_input.rect) &&
            !CheckCollisionPointRec(mouse, start_input.rect) &&
            !CheckCollisionPointRec(mouse, end_input.rect)) {
            selected = -1;
        }
    }

    // ───── DRAGGING ─────
    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && dragging != -1) {
        time_t cursor_time = tracker.view_start + (time_t)((mouse.x - 100.0f) * secs_per_pixel);
        Entry *e = &tracker.entries[dragging];

        if (drag_mode == 0) {                                   // move whole bar
            time_t dur = e->end - e->start;
            e->start = cursor_time + drag_offset;
            e->end   = e->start + dur;
        }
        else if (drag_mode == 1) {                              // resize left
            time_t ns = cursor_time + drag_offset;
            if (ns < e->end - 86400) e->start = ns;
        }
        else if (drag_mode == 2) {                              // resize right
            time_t ne = cursor_time + drag_offset;
            if (ne > e->start +  + 86400) e->end = ne;
        }
        e->duration_years = difftime(e->end, e->start) / (365.25*86400.0);
        if (selected == dragging) {
            SyncInputsToSelected();   // live update while dragging!
        }
    }

    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        dragging = -1;
        drag_mode = 0;
        if (selected >= 0) SyncInputsToSelected();
    }
}

void HandleKeyboardShortcuts(void) {
    if (IsKeyPressed(KEY_ENTER) && selected == -1) {
        time_t s = 0, e = 0;

        // Use current text if valid, otherwise fall back to today
        if (strlen(start_input.text) > 0) s = ParseDateTime(start_input.text);
        if (s == 0) {
            s = time(NULL);
            struct tm *tm = localtime(&s);
            tm->tm_hour = tm->tm_min = tm->tm_sec = 0;
            s = mktime(tm);
        }

        if (strlen(end_input.text) > 0) e = ParseDateTime(end_input.text);
        if (e == 0 || e <= s) {
            e = s + 365*86400;  // default: +1 year
        }

        if (tracker.count < MAX_ENTRIES) {
            Entry *en = &tracker.entries[tracker.count++];
            strncpy(en->name, name_input.text[0] ? name_input.text : "Untitled", MAX_NAME-1);
            en->start = s;
            en->end   = e;
            en->duration_years = difftime(e, s) / (365.25*86400);
            en->color = (Color){GetRandomValue(90,230), GetRandomValue(90,230), GetRandomValue(110,240), 255};

            selected = tracker.count - 1;
            SyncInputsToSelected();
            last_selected = -2;
        }
    }

    if (IsKeyPressed(KEY_DELETE) && selected >= 0) {
        memmove(&tracker.entries[selected], &tracker.entries[selected+1],
                sizeof(Entry) * (tracker.count - selected - 1));
        tracker.count--; selected = -1; last_selected = -2;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────────────────────────────────────
// Helper function — replaces the C++ lambda
static struct tm* SafeLocalTime(const time_t* timep) {
    static struct tm tm_buf;
    localtime_r(timep, &tm_buf);  // thread-safe
    return &tm_buf;
}

void DrawTimelineGrid(void)
{
    const float left       = 100.0f;
    const float right      = GetScreenWidth() - 50.0f;
    const float baseline_y = 260.0f;

    double secs_per_pixel = (365.25 * 86400.0) / tracker.pixels_per_year;
    double view_seconds   = (right - left) * secs_per_pixel;
    time_t view_end       = tracker.view_start + (time_t)view_seconds;

    DrawLineEx((Vector2){left, baseline_y}, (Vector2){right, baseline_y},
               3.0f, (Color){90, 90, 140, 255});
    /* ────────────────────── TODAY LINE ────────────────────── */
    time_t now = time(NULL);
    struct tm today = {0};
    localtime_r(&now, &today);
    today.tm_hour = today.tm_min = today.tm_sec = 0;
    today.tm_isdst = -1;                      // ← also here
    time_t today_midnight = mktime(&today);

    double secs = difftime(today_midnight, tracker.view_start);
    float tx = left + (float)(secs / secs_per_pixel);

    if (tx >= left - 200 && tx <= right + 200)
    {
        DrawLineEx((Vector2){tx, baseline_y - 60}, (Vector2){tx, GetScreenHeight() - 50},
                   4.5f, RED);
        DrawCircle(tx, baseline_y, 10, RED);
        DrawCircle(tx, baseline_y, 7, (Color){40,10,10,255});
        DrawTextEx(font, "TODAY", (Vector2){tx + 14, baseline_y + 40}, 28, 1.3f, RED);
    }
    /* ────────────────────── YEARS — EVERY YEAR, SAME HEIGHT, NEVER TOUCH TICKS ────────────────────── */
    if (tracker.pixels_per_year > 30.0)
    {
        // Start far enough back to never miss the first visible year
        struct tm start_tm = {0};
        localtime_r(&tracker.view_start, &start_tm);
        start_tm.tm_year -= 50;      // huge safety buffer
        start_tm.tm_mon = 0;
        start_tm.tm_mday = 1;
        start_tm.tm_isdst = -1;
        mktime(&start_tm);
        int year = start_tm.tm_year + 1900;

        int end_year = SafeLocalTime(&view_end)->tm_year + 1900 + 50;

        for (; year <= end_year; ++year)
        {
            struct tm ytm = {0};
            ytm.tm_year = year - 1900;
            ytm.tm_mon  = 0;
            ytm.tm_mday = 1;
            ytm.tm_isdst = -1;
            time_t yt = mktime(&ytm);

            double secs = difftime(yt, tracker.view_start);
            float x = left + (float)(secs / secs_per_pixel);

            // Draw only if near screen
            if (x < left - 600 || x > right + 600) continue;

            char buf[16];
            snprintf(buf, sizeof(buf), "%d", year);

            // 1. Tick — short and clean
            DrawLineEx((Vector2){x, baseline_y - 28},
                       (Vector2){x, baseline_y + 28}, 4.0f, WHITE);

            // 2. Label — ALL at exactly the same Y level, far above everything
            const float LABEL_Y = baseline_y - 160.0f;   // ← fixed height for all years

            DrawTextPro(font, buf,
                        (Vector2){x + 16, LABEL_Y},
                        (Vector2){0, 0},
                        90.0f,       // rotated
                        36,          // big and bold
                        1.5f,
                        WHITE);
        }
    }
                            
    /* ────────────────────── MONTHS ────────────────────── */
    if (tracker.pixels_per_year > 250.0)
    {
        struct tm tmp = {0};
        localtime_r(&tracker.view_start, &tmp);   // fills tm_year, tm_mon, tm_mday, etc.
        tmp.tm_mday = 1;
        tmp.tm_hour = tmp.tm_min = tmp.tm_sec = 0;
        tmp.tm_isdst = -1;                        // ← THIS WAS MISSING BEFORE!
        time_t t = mktime(&tmp);

        if (t < tracker.view_start)
        {
            if (++tmp.tm_mon >= 12) { tmp.tm_mon = 0; tmp.tm_year++; }
            tmp.tm_isdst = -1;                    // ← again
            t = mktime(&tmp);
        }

        while (t < view_end + 86400LL*60)
        {
            double secs = difftime(t, tracker.view_start);
            float x = left + (float)(secs / secs_per_pixel);

            if (x >= left - 200 && x <= right + 200)
            {
                struct tm *mtm = SafeLocalTime(&t);
                float thickness = (mtm->tm_mon == 0) ? 2.8f : 1.9f;
                float height_up = (mtm->tm_mon == 0) ? 22 : 15;
                Color col = (mtm->tm_mon == 0) ? Fade(WHITE, 0.95f) : Fade(WHITE, 0.65f);

                DrawLineEx((Vector2){x, baseline_y - height_up},
                           (Vector2){x, baseline_y + 14}, thickness, col);

                char label[16];
                strftime(label, sizeof(label), "%b", mtm);
                DrawTextPro(font, label,
                            (Vector2){x + 10, baseline_y - 52},
                            (Vector2){0,0}, 90.0f, 17, 1.2f, Fade(WHITE, 0.9f));
            }

            // Advance one month
            if (++tmp.tm_mon >= 12) { tmp.tm_mon = 0; tmp.tm_year++; }
            tmp.tm_mday = 1;
            tmp.tm_isdst = -1;                    // ← CRITICAL: set every time
            t = mktime(&tmp);
        }
    }

    /* ────────────────────── DAYS (weekly dividers) ────────────────────── */
    if (tracker.pixels_per_year > 3000.0)
    {
        struct tm tmp = {0};
        localtime_r(&tracker.view_start, &tmp);
        tmp.tm_hour = tmp.tm_min = tmp.tm_sec = 0;
        tmp.tm_isdst = -1;                        // ← CRITICAL
        time_t t = mktime(&tmp);

        if (t < tracker.view_start) {
            tmp.tm_mday++;
            tmp.tm_isdst = -1;                    // ← again
            t = mktime(&tmp);
        }

        time_t stop = view_end + 86400 * 10;

        while (t < stop)
        {
            double secs = difftime(t, tracker.view_start);
            float x = left + (float)(secs / secs_per_pixel);

            if (x >= left - 100 && x <= right + 100)
            {
                struct tm *dtm = SafeLocalTime(&t);
                int day = dtm->tm_mday;

                bool is_month_start  = (day == 1);
                bool is_week_divider = (day == 8 || day == 15 || day == 22 || day == 29);

                float thickness = is_month_start ? 2.8f : (is_week_divider ? 2.1f : 1.0f);
                float height    = is_month_start ? 22.0f : (is_week_divider ? 16.0f : 9.0f);
                Color col       = Fade(WHITE, is_month_start ? 0.90f : (is_week_divider ? 0.75f : 0.38f));

                DrawLineEx((Vector2){x, baseline_y - height},
                           (Vector2){x, baseline_y + 10}, thickness, col);

                if (is_month_start || is_week_divider || tracker.pixels_per_year > 20000.0)
                {
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%d", day);
                    DrawTextPro(font, buf,
                                (Vector2){x + 8, baseline_y - 62},
                                (Vector2){0,0}, 90.0f, 11, 1.0f, Fade(WHITE, 0.85f));
                }
            }

            tmp.tm_mday++;
            tmp.tm_isdst = -1;                    // ← CRITICAL: every single day
            t = mktime(&tmp);
        }
    }
}

void DrawEvents(void) {
    Vector2 mouse = GetMousePosition();

    // NEW: compact vertical spacing — was 72, now 36 → twice as many events!
    const float row_height = 36.0f;
    const float start_y    = 300.0f;   // start lower to make room above

    for (int i = 0; i < tracker.count; i++) {
        Entry *e = &tracker.entries[i];

        double secs_from_view = difftime(e->start, tracker.view_start);
        float x = 100.0f + (float)(secs_from_view / (365.25*86400.0) * tracker.pixels_per_year);

        float w = (float)(e->duration_years * tracker.pixels_per_year);
        if (w < 5.0f) w = 5.0f;        // minimum visible width

        float y = start_y + i * row_height;

        Rectangle rec = { x, y, w, 28.0f };   // height 28 instead of 50

        // Hover / selected colors
        bool hover = CheckCollisionPointRec(mouse, rec);
        Color col = e->color;
        if (dragging == i)     col = Fade(col, 1.4f);
        else if (hover)        col = Fade(col, 0.9f);
        else if (selected == i) col = Fade(col, 1.1f);

        DrawRectangleRec(rec, col);
        if (selected == i) DrawRectangleLinesEx(rec, 3.0f, YELLOW);

        // Edge grab handles (only when hovered or dragging)
        if (hover || dragging == i) {
            DrawRectangle(x, y, 16, 28, Fade(WHITE, 0.3f));
            DrawRectangle(x + w - 16, y, 16, 28, Fade(WHITE, 0.3f));
        }

        // Name — smaller font, clipped smartly
        const char* name = e->name;
        if (TextLength(name) * 10 > (unsigned int)w - 40) {
            // Auto-truncate long names with "..."
            static char short_name[64];
            strncpy(short_name, name, 20);
            short_name[20] = '\0';
            strcat(short_name, "...");
            name = short_name;
        }
        DrawTextEx(font, name, (Vector2){x + 10, y + 6}, 18, 1, WHITE);

        // Duration in corner
        char dur[32];
        if (e->duration_years < 1.0)
            snprintf(dur, sizeof(dur), "%.1f mo", e->duration_years * 12.0);
        else
            snprintf(dur, sizeof(dur), "%.1f y", e->duration_years);

        float tw = MeasureTextEx(font, dur, 16, 1).x;
        DrawTextEx(font, dur, (Vector2){x + w - tw - 8, y + 8}, 16, 1, Fade(WHITE, 0.9f));
    }
}

void DrawUI(void) {
    int W = GetScreenWidth(), H = GetScreenHeight();

    DrawRectangle(0, 0, W, 90, (Color){20,20,35,255});
    DrawLine(0, 90, W, 90, (Color){60,60,80,255});

    DrawTextEx(font, "Name:",   (Vector2){100, 30}, 22, 1, (Color){200,200,220,255});
    DrawTextEx(font, "Start:",  (Vector2){620, 30}, 22, 1, (Color){200,200,220,255});
    DrawTextEx(font, "End:",    (Vector2){900, 30}, 22, 1, (Color){200,200,220,255});

    DrawTextInput(&name_input,   font);
    DrawTextInput(&start_input,  font);
    DrawTextInput(&end_input,    font);

    DrawTextEx(font,
        "LClick=select • Drag edges=resize • RDrag=pan • Scroll=zoom • Enter=new • Del=remove",
        (Vector2){15, H-32}, 18, 1, (Color){160,180,220,255});
}

// ─────────────────────────────────────────────────────────────────────────────
// Text Input Implementation
// ─────────────────────────────────────────────────────────────────────────────
static float TextWidthUpTo(Font font, const char *text, int count, float size, float spacing) {
    if (count <= 0) return 0;
    char tmp[MAX_INPUT];
    int len = (int)fmin(count, strlen(text));
    strncpy(tmp, text, len); tmp[len] = '\0';
    return MeasureTextEx(font, tmp, size, spacing).x;
}

void DrawTextInput(TextInput *ti, Font font) {
    Color bg = ti->active ? (Color){50,80,140,255} : (Color){40,40,50,255};
    Color border = ti->active ? SKYBLUE : GRAY;
    DrawRectangleRec(ti->rect, bg);
    DrawRectangleLinesEx(ti->rect, 2, border);
    const char *display = ti->text[0] ? ti->text : "(empty)";
    DrawTextEx(font, display, (Vector2){ti->rect.x + 12, ti->rect.y + 12}, 20, 1, WHITE);
    if (ti->active && ((int)(GetTime() * 2) % 2 == 0)) {
        float x = ti->rect.x + 12 + TextWidthUpTo(font, ti->text, ti->cursor_pos, 20, 1);
        DrawRectangle((int)x, (int)ti->rect.y + 12, 2, 20, WHITE);
    }
}

void UpdateTextInput(TextInput *ti, Font font) {
    Vector2 mouse = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        bool was_active = ti->active;
        ti->active = CheckCollisionPointRec(mouse, ti->rect);
        if (ti->active && !was_active) {
            float rel_x = mouse.x - (ti->rect.x + 12);
            if (rel_x < 0) ti->cursor_pos = 0;
            else {
                int low = 0, high = (int)strlen(ti->text);
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
            memmove(ti->text + ti->cursor_pos + 1, ti->text + ti->cursor_pos, strlen(ti->text + ti->cursor_pos) + 1);
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

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────
int main(void) {
    const int W = 1500, H = 900;
    InitWindow(W, H, "Lifetime Visual Time Tracker");
    SetTargetFPS(60);

    font = LoadFontEx("/usr/share/fonts/TTF/DejaVuSans.ttf", 22, NULL, 0);
    if (font.texture.id == 0) font = GetFontDefault();

    // Load saved data first (so we keep any custom zoom/view if saved)
    LoadTracker("timetracker.json");

    // Set default zoom level
    tracker.pixels_per_year = 700.0;

    // ───── CENTER TODAY ON SCREEN ─────
    time_t now = time(NULL);   // ← only declared once now

    struct tm today_tm = {0};
    localtime_r(&now, &today_tm);
    today_tm.tm_hour = today_tm.tm_min = today_tm.tm_sec = 0;
    today_tm.tm_isdst = -1;
    time_t today_midnight = mktime(&today_tm);

    // Calculate how many seconds are visible on screen
    double visible_pixels = GetScreenWidth() - 150.0;  // left + right margin
    double visible_seconds = visible_pixels * (365.25 * 86400.0) / tracker.pixels_per_year;

    // Center today exactly in the middle
    tracker.view_start = today_midnight - (time_t)(visible_seconds / 2.0);

    // Optional: gentle bounds to prevent insane views
    time_t min_view = today_midnight - 200LL * 365 * 86400;  // max 200 years back
    time_t max_view = today_midnight + 100LL * 365 * 86400;  // max 100 years forward
    if (tracker.view_start < min_view) tracker.view_start = min_view;
    if (tracker.view_start > max_view) tracker.view_start = max_view;

    // Initialize text inputs with today's date
    char today_str[32];
    strftime(today_str, sizeof(today_str), "%Y-%m-%d", localtime(&now));

    InitTextInput(&name_input,   (Rectangle){180, 20, 420, 48}, "");
    InitTextInput(&start_input,  (Rectangle){680, 20, 200, 48}, today_str);
    InitTextInput(&end_input,    (Rectangle){960, 20, 200, 48}, today_str);
    
    while (!WindowShouldClose()) {
        HandlePanningAndZooming();
        HandleSelectionAndDragging();
        UpdateTextInput(&name_input, font);
        UpdateTextInput(&start_input, font);
        UpdateTextInput(&end_input, font);
        HandleKeyboardShortcuts();

        if (selected != last_selected) {
            SyncInputsToSelected();
            last_selected = selected;
        }
        if (selected >= 0) {
            static char last_name[128] = "", last_start[128] = "", last_end[128] = "";
            if (strcmp(name_input.text, last_name) || strcmp(start_input.text, last_start) || strcmp(end_input.text, last_end)) {
                ApplyInputsToSelected();
                strcpy(last_name, name_input.text);
                strcpy(last_start, start_input.text);
                strcpy(last_end, end_input.text);
            }
        }

        BeginDrawing();
        ClearBackground((Color){12,12,28,255});
        DrawTimelineGrid();
        DrawEvents();
        DrawUI();
        EndDrawing();
    }

    SaveTracker("timetracker.json");
    UnloadFont(font);
    CloseWindow();
    return 0;
}
