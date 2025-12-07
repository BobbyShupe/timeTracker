#define _GNU_SOURCE
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>          // ← THIS WAS MISSING

#define MAX_ENTRIES 1000
#define MAX_NAME    64
#define MAX_INPUT   1024
#define EDGE_GRAB   20

typedef struct { char name[MAX_NAME]; time_t start, end; double duration_years; Color color; char description[512]; } Entry;
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
static TextInput name_input, start_input, end_input, desc_input;
static int selected = -1;
static int dragging = -1;
static int drag_mode = 0;
static time_t drag_offset = 0;
static int last_selected = -1;

static const float timeline_y = 140.0f;
static const float events_start_y = timeline_y + 160.0f;   // ← YOUR desired offset

static time_t original_duration = 0;
static int    g_track_of_event[1024] = {0};
static double secs_per_pixel = 0.0;
static time_t track_free_until[50];
static bool clicked_on_event_this_frame = false;
static bool  g_show_tooltip = false;
static char  g_tooltip_text[512];
static float g_tooltip_x, g_tooltip_y;

#define EDGE_GRAB_PIXELS 16.0f   // use this instead of EDGE_GRAB to avoid conflict
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
        // Escape quotes in description
        char desc_esc[1024] = {0};
        for (int j = 0, k = 0; tracker.entries[i].description[j] && k < 1024; j++) {
            if (tracker.entries[i].description[j] == '"') { desc_esc[k++] = '\\'; }
            desc_esc[k++] = tracker.entries[i].description[j];
        }
        fprintf(f, "  {\"name\":\"%s\",\"start\":\"%s\",\"end\":\"%s\",\"desc\":\"%s\"}%s\n",
                tracker.entries[i].name, s1, s2, desc_esc,
                i < tracker.count-1 ? "," : "");
    }
    fprintf(f, "]\n"); fclose(f);
}

void LoadTracker(const char *file) {
    FILE *f = fopen(file, "r"); if (!f) return;
    char line[512]; tracker.count = 0;
    while (fgets(line, sizeof(line), f) && tracker.count < MAX_ENTRIES) {
        char name[MAX_NAME] = {0}, start[64] = {0}, end[64] = {0}, desc[512] = {0};
        // Try with desc first, then fallback without
        if (sscanf(line, "  {\"name\":\"%63[^\"]\",\"start\":\"%63[^\"]\",\"end\":\"%63[^\"]\",\"desc\":\"%511[^\"]\"}", 
                   name, start, end, desc) >= 3 ||
            sscanf(line, "  {\"name\":\"%63[^\"]\",\"start\":\"%63[^\"]\",\"end\":\"%63[^\"]\"}", 
                   name, start, end) == 3) {
            time_t s = ParseDateTime(start);
            time_t e = ParseDateTime(end);
            if (s && e > s) {
                Entry *en = &tracker.entries[tracker.count++];
                strncpy(en->name, name, MAX_NAME-1);
                en->name[MAX_NAME-1] = '\0';
                strncpy(en->description, desc, 511);
                en->description[511] = '\0';           // ← Keep loaded description
                en->start = s; 
                en->end = e;
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
    strncpy(desc_input.text, e->description, MAX_INPUT-1); desc_input.text[MAX_INPUT-1] = '\0';
    strftime(start_input.text, MAX_INPUT, "%Y-%m-%d", localtime(&e->start));
    strftime(end_input.text,   MAX_INPUT, "%Y-%m-%d", localtime(&e->end));
}

void ApplyInputsToSelected(void) {
    if (selected < 0 || selected >= tracker.count) return;
    Entry *e = &tracker.entries[selected];
    time_t s = ParseDateTime(start_input.text);
    time_t e_time = ParseDateTime(end_input.text);
    if (s && e_time > s) {
        strncpy(e->name, name_input.text[0] ? name_input.text : "Untitled", MAX_NAME-1);
        strncpy(e->description, desc_input.text, 511);
        e->description[511] = '\0';
        e->start = s;
        e->end = e_time;
        e->duration_years = difftime(e->end, e->start) / (365.25*86400);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Input Handling – FULLY FIXED VERSION
// ─────────────────────────────────────────────────────────────────────────────
void HandlePanningAndZooming(void)
{
    const float left = 100.0f;
    double secs_per_pixel_local = (365.25 * 86400.0) / tracker.pixels_per_year;
    secs_per_pixel = secs_per_pixel_local;

    Vector2 screen_center = { GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f };
    bool both_buttons = IsMouseButtonDown(MOUSE_LEFT_BUTTON) && IsMouseButtonDown(MOUSE_RIGHT_BUTTON);

    static bool panning = false;
    static bool dual_zoom_active = false;

    // ───── PANNING (Right button only) ─────
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON) && !both_buttons)
    {
        if (!panning)
        {
            panning = true;
            HideCursor();
            SetMousePosition((int)screen_center.x, (int)screen_center.y);
        }
        else
        {
            Vector2 mouse = GetMousePosition();
            Vector2 delta = { mouse.x - screen_center.x, mouse.y - screen_center.y };

            if (fabsf(delta.x) > 0.1f)
                tracker.view_start -= (time_t)(delta.x * secs_per_pixel_local);

            SetMousePosition((int)screen_center.x, (int)screen_center.y);
        }
    }
    else if (panning)
    {
        panning = false;
        ShowCursor();
    }

    // ───── DUAL-BUTTON ZOOM (Left + Right = zoom with hidden centered cursor) ─────
    if (both_buttons)
    {
        if (!dual_zoom_active)
        {
            dual_zoom_active = true;
            HideCursor();
            SetMousePosition((int)screen_center.x, (int)screen_center.y);
        }
        else
        {
            Vector2 mouse = GetMousePosition();
            float dy = mouse.y - screen_center.y;  // vertical movement from center

            if (fabsf(dy) > 2.0f)
            {
                time_t time_under_mouse = tracker.view_start + (time_t)((mouse.x - left) * secs_per_pixel_local);

                float speed = 0.012f;  // tweak this for faster/slower zoom
                double zoom_factor = powf(1.25, dy * speed);

                tracker.pixels_per_year *= zoom_factor;
                tracker.pixels_per_year = fmaxf(20.0, fminf(tracker.pixels_per_year, 2000000.0));

                secs_per_pixel_local = (365.25 * 86400.0) / tracker.pixels_per_year;
                secs_per_pixel = secs_per_pixel_local;

                tracker.view_start = time_under_mouse - (time_t)((mouse.x - left) * secs_per_pixel_local);

                SetMousePosition((int)screen_center.x, (int)screen_center.y);  // keep locked
            }
        }
    }
    else if (dual_zoom_active)
    {
        dual_zoom_active = false;
        ShowCursor();
    }

    // ───── MOUSE WHEEL ZOOM (unchanged) ─────
    float wheel = GetMouseWheelMoveV().y;
    if (wheel == 0.0f) wheel = GetMouseWheelMove();
    if (wheel != 0.0f)
    {
        Vector2 mouse = GetMousePosition();
        time_t time_under_mouse = tracker.view_start + (time_t)((mouse.x - left) * secs_per_pixel_local);

        double zoom_factor = (wheel > 0) ? 1.25 : 0.80;
        tracker.pixels_per_year *= zoom_factor;
        tracker.pixels_per_year = fmaxf(20.0, fminf(tracker.pixels_per_year, 2000000.0));

        secs_per_pixel_local = (365.25 * 86400.0) / tracker.pixels_per_year;
        secs_per_pixel = secs_per_pixel_local;

        tracker.view_start = time_under_mouse - (time_t)((mouse.x - left) * secs_per_pixel_local);
    }
}

void HandleSelectionAndDragging(void) {
    if (dragging == -1) return;

    Vector2 mouse = GetMousePosition();
    time_t cursor_time = tracker.view_start + (time_t)((mouse.x - 0.0f) * secs_per_pixel);
    Entry *e = &tracker.entries[dragging];        // ← fixed: added ]

    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
        if (drag_mode == 0) {
            e->start = cursor_time + drag_offset;
            e->end   = e->start + original_duration;
        }
        else if (drag_mode == 1) {
            time_t ns = cursor_time + drag_offset;
            if (ns < e->end - 86400) e->start = ns;
        }
        else if (drag_mode == 2) {
            time_t ne = cursor_time + drag_offset;
            if (ne > e->start + 86400) e->end = ne;
        }

        e->duration_years = difftime(e->end, e->start) / (365.25 * 86400.0);
        if (selected == dragging) SyncInputsToSelected();
    }

    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        dragging = -1;
        drag_mode = 0;
        original_duration = 0;
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
            en->name[MAX_NAME-1] = '\0';
            strncpy(en->description, desc_input.text, 511);
            en->description[511] = '\0';           // ← Fixed: now 511 (was 255)
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
        tracker.count--; 
        selected = -1; 
        last_selected = -2;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────────────────────────────────────
// Helper function — replaces the C++ lambda
static struct tm* SafeLocalTime(const time_t* timep) {
    static struct tm tm_buf;
    localtime_r(timep, &tm_buf);
    return &tm_buf;
}

void DrawTimelineGrid(void)
{
    const float left       = 0.0f;
    const float right      = GetScreenWidth();
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
    today.tm_isdst = -1;
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

/* ────────────────────── VERTICAL YEAR LABELS – FULL YYYY + CLEAN SPACING ────────────────────── */
if (tracker.pixels_per_year > 30.0f)
{
    struct tm start_tm = {0};
    localtime_r(&tracker.view_start, &start_tm);
    start_tm.tm_year -= 50;
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

        if (x < left - 600 || x > right + 600) continue;

        // Tick
        DrawLineEx((Vector2){x, baseline_y - 28},
                   (Vector2){x, baseline_y + 28}, 4.0f, WHITE);

        char buf[8];
        snprintf(buf, sizeof(buf), "%d", year);

        const float fontSize = 36.0f;
        Vector2 textSize = MeasureTextEx(font, buf, fontSize, 1.5f);

        // NEW STRATEGY: Move the entire label higher up (above the month area)
        // We place the TOP of the text at y = baseline_y - 210
float top_of_text_y = baseline_y - 200.0f;        // original beautiful position
float y_origin = top_of_text_y + textSize.y;     // full 4 digits visible

DrawTextPro(font, buf,
            (Vector2){x + 16, y_origin},
            (Vector2){0, 0},
            90.0f, 36.0f, 1.5f, WHITE);
    }
}

    /* ────────────────────── MONTH & DAY GRIDS (unchanged) ────────────────────── */
    if (tracker.pixels_per_year > 250.0f)
    {
        struct tm tmp = {0};
        localtime_r(&tracker.view_start, &tmp);
        tmp.tm_mday = 1;
        tmp.tm_hour = tmp.tm_min = tmp.tm_sec = 0;
        tmp.tm_isdst = -1;
        time_t t = mktime(&tmp);

        if (t < tracker.view_start)
        {
            if (++tmp.tm_mon >= 12) { tmp.tm_mon = 0; tmp.tm_year++; }
            tmp.tm_isdst = -1;
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
                            (Vector2){x + 10, baseline_y - 65},
                            (Vector2){0,0}, 90.0f, 17, 1.2f, Fade(WHITE, 0.9f));
            }

            if (++tmp.tm_mon >= 12) { tmp.tm_mon = 0; tmp.tm_year++; }
            tmp.tm_mday = 1;
            tmp.tm_isdst = -1;
            t = mktime(&tmp);
        }
    }

    if (tracker.pixels_per_year > 3000.0f)
    {
        struct tm tmp = {0};
        localtime_r(&tracker.view_start, &tmp);
        tmp.tm_hour = tmp.tm_min = tmp.tm_sec = 0;
        tmp.tm_isdst = -1;
        time_t t = mktime(&tmp);

        if (t < tracker.view_start) {
            tmp.tm_mday++;
            tmp.tm_isdst = -1;
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

                if (is_month_start || is_week_divider || tracker.pixels_per_year > 20000.0f)
                {
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%d", day);
                    DrawTextPro(font, buf,
                                (Vector2){x + 8, baseline_y - 62},
                                (Vector2){0,0}, 90.0f, 11, 1.0f, Fade(WHITE, 0.85f));
                }
            }

            tmp.tm_mday++;
            tmp.tm_isdst = -1;
            t = mktime(&tmp);
        }
    }
}

void DrawEvents(void) {
    Vector2 mouse = GetMousePosition();
    const float row_spacing    = 10.0f;
    const float line_thickness = 3.5f;
    const int   max_tracks     = 50;

    secs_per_pixel = (365.25 * 86400.0) / tracker.pixels_per_year;

    clicked_on_event_this_frame = false;
    g_show_tooltip = false;  // ← important: reset every frame

    // Sort events by start time
    int order[MAX_ENTRIES];
    for (int i = 0; i < tracker.count; i++) order[i] = i;
    for (int i = 0; i < tracker.count - 1; i++)
        for (int j = i + 1; j < tracker.count; j++)
            if (tracker.entries[order[i]].start > tracker.entries[order[j]].start)
                { int tmp = order[i]; order[i] = order[j]; order[j] = tmp; }

    // Track assignment (vertical stacking)
    for (int t = 0; t < max_tracks; t++) track_free_until[t] = 0;
    for (int k = 0; k < tracker.count; k++) {
        int i = order[k]; Entry *e = &tracker.entries[i];
        int track = 0;
        while (track < max_tracks && e->start < track_free_until[track]) track++;
        if (track >= max_tracks) track = max_tracks - 1;
        track_free_until[track] = e->end;
        g_track_of_event[i] = track;
    }

    for (int i = 0; i < tracker.count; i++) {
        Entry *e = &tracker.entries[i];

        double secs_from_view = difftime(e->start, tracker.view_start);
        float x_start = (float)(secs_from_view * tracker.pixels_per_year / (365.25 * 86400.0));
        float duration_px = e->duration_years * tracker.pixels_per_year;
        if (duration_px < 2.0f) duration_px = 2.0f;

        float draw_x1 = fmaxf(x_start, 0.0f);
        float draw_x2 = x_start + duration_px;
        float draw_len = draw_x2 - draw_x1;
        if (draw_len <= 0.0f) continue;

        float y = events_start_y + g_track_of_event[i] * row_spacing;

        Rectangle hit = { draw_x1, y - 7, draw_len, 16 };
        bool hovered = CheckCollisionPointRec(mouse, hit);

        if (hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) clicked_on_event_this_frame = true;
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && hovered && dragging == -1) {
            selected = i; dragging = i;
            float rel_x = mouse.x - draw_x1;
            drag_mode = (rel_x < EDGE_GRAB_PIXELS) ? 1 :
                        (rel_x > draw_len - EDGE_GRAB_PIXELS) ? 2 : 0;
            time_t cursor_time = tracker.view_start + (time_t)((mouse.x) * secs_per_pixel);
            if (drag_mode == 1)      drag_offset = e->start - cursor_time;
            else if (drag_mode == 2) drag_offset = e->end   - cursor_time;
            else { drag_offset = e->start - cursor_time; original_duration = e->end - e->start; }
        }

        // Colors & drawing
        bool is_selected = (selected == i);
        bool is_dragging = (dragging == i);
        Color col = is_dragging ? RED :
                    is_selected ? (Color){255,70,70,255} :
                    hovered     ? (Color){255,130,130,255} : (Color){240,40,40,255};

        DrawLineEx((Vector2){draw_x1, y}, (Vector2){draw_x1 + draw_len, y}, line_thickness, col);

        DrawRing((Vector2){x_start, y}, 4.6f, 5.4f, 0, 360, 32, Fade(WHITE, 0.75f));
        DrawRing((Vector2){x_start, y}, 2.6f, 3.4f, 0, 360, 32, col);
        DrawRing((Vector2){draw_x2, y}, 4.6f, 5.4f, 0, 360, 32, Fade(WHITE, 0.75f));
        DrawRing((Vector2){draw_x2, y}, 2.6f, 3.4f, 0, 360, 32, col);

        if (is_selected || is_dragging) {
            DrawRing((Vector2){x_start, y}, 5.8f, 6.8f, 0, 360, 32, Fade(YELLOW, 0.45f));
            DrawRing((Vector2){draw_x2, y}, 5.8f, 6.8f, 0, 360, 32, Fade(YELLOW, 0.45f));
        }

        // Name label
        if (draw_len >= 20.0f) {
            const char* name = e->name[0] ? e->name : "Untitled";
            float fs = 12.0f;
            Vector2 size = MeasureTextEx(font, name, fs, 1.0f);
            if (size.x > duration_px - 10) {
                static char buf[64];
                strncpy(buf, name, 20); strcpy(buf + 20, "..."); buf[23] = '\0';
                name = buf;
            }
            float tx = x_start + (duration_px - size.x) * 0.5f;
            DrawTextEx(font, name, (Vector2){tx + 1, y - 7 + 1}, fs, 1, Fade(BLACK, 0.6f));
            DrawTextEx(font, name, (Vector2){tx, y - 7}, fs, 1, WHITE);
        }

        // ───── TOOLTIP: ONLY STORE DATA HERE (drawn later by DrawGlobalTooltip) ─────
        if (hovered && e->description[0]) {
            strncpy(g_tooltip_text, e->description, 511);
            g_tooltip_text[511] = '\0';
            g_tooltip_x = mouse.x;
            g_tooltip_y = mouse.y;
            g_show_tooltip = true;
        }
    }

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !clicked_on_event_this_frame && selected >= 0) {
        selected = -1;
        dragging = -1;
        SyncInputsToSelected();
    }
}

void DrawGlobalTooltip(void)
{
    if (!g_show_tooltip) return;

    const float fontSize = 19.0f;
    const float spacing  = 1.2f;
    const float maxWidth = 640.0f;
    const float pad      = 24.0f;
    const float lineH    = fontSize + 10.0f;

    // ── Measure total height ──
    float totalH = pad * 2.0f;
    float lineW  = 0.0f;
    const char* p = g_tooltip_text;

    while (*p) {
        if (*p == '\n') {
            totalH += lineH;
            lineW = 0.0f;
            p++;
            continue;
        }

        const char* start = p;
        while (*p && *p != ' ' && *p != '\n') p++;
        int len = (int)(p - start);
        if (len == 0) { p++; continue; }

        float wordW = MeasureTextEx(font, TextFormat("%.*s", len, start), fontSize, spacing).x;
        float spaceW = (lineW > 0.0f) ? MeasureTextEx(font, " ", fontSize, spacing).x : 0.0f;

        if (lineW + spaceW + wordW > maxWidth - pad*2 && lineW > 0.0f) {
            totalH += lineH;
            lineW = 0.0f;
        }
        lineW += spaceW + wordW;

        if (*p == ' ') p++;
    }
    if (lineW > 0.0f) totalH += lineH;

    // ── Position tooltip ──
    float x = g_tooltip_x + 30.0f;
    float y = g_tooltip_y - totalH - 20.0f;
    if (y < 160.0f) y = g_tooltip_y + 40.0f;

    float w = maxWidth + pad * 2.0f;
    if (x + w > GetScreenWidth() - 20.0f) x = GetScreenWidth() - w - 20.0f;
    if (x < 20.0f) x = 20.0f;

    Rectangle rect = { x, y, w, totalH };

    // ── SOLID background (drawn last = always on top) ──
    DrawRectangleRounded(rect, 0.3f, 16, (Color){8, 10, 22, 255});           // 100% opaque
    Rectangle inner = { x + 6, y + 6, w - 12, totalH - 12 };
    DrawRectangleRounded(inner, 0.25f, 14, (Color){16, 20, 38, 255});
    DrawRectangleRoundedLinesEx(rect, 0.3f, 16, 4.0f, (Color){100, 200, 255, 255});

    // ── Draw wrapped text ──
    float cx = x + pad;
    float cy = y + pad;
    p = g_tooltip_text;
    lineW = 0.0f;

    while (*p) {
        if (*p == '\n') {
            cy += lineH;
            cx = x + pad;
            lineW = 0.0f;
            p++;
            continue;
        }

        const char* start = p;
        while (*p && *p != ' ' && *p != '\n') p++;
        int len = (int)(p - start);
        if (len == 0) { p++; continue; }

        char word[512];
        snprintf(word, sizeof(word), "%.*s", len, start);

        float wordW = MeasureTextEx(font, word, fontSize, spacing).x;
        float spaceW = (lineW > 0.0f) ? MeasureTextEx(font, " ", fontSize, spacing).x : 0.0f;

        if (lineW + spaceW + wordW > w - pad*2 && lineW > 0.0f) {
            cy += lineH;
            cx = x + pad;
            lineW = 0.0f;
        }

        DrawTextEx(font, word, (Vector2){cx + 1, cy + 1}, fontSize, spacing, Fade(BLACK, 0.8f));
        DrawTextEx(font, word, (Vector2){cx,     cy},     fontSize, spacing, WHITE);

        cx += wordW + spaceW;
        lineW += wordW + spaceW;

        if (*p == ' ') p++;
    }

    // Reset for next frame
    g_show_tooltip = false;
}

void DrawUI(void) {
    int W = GetScreenWidth(), H = GetScreenHeight();

    DrawRectangle(0, 0, W, 140, (Color){20,20,35,255});  // taller header
    DrawLine(0, 140, W, 140, (Color){60,60,80,255});

    DrawTextEx(font, "Name:",        (Vector2){100, 30}, 22, 1, (Color){200,200,220,255});
    DrawTextEx(font, "Start:",       (Vector2){620, 30}, 22, 1, (Color){200,200,220,255});
    DrawTextEx(font, "End:",         (Vector2){900, 30}, 22, 1, (Color){200,200,220,255});
    DrawTextEx(font, "Description:", (Vector2){100, 90}, 22, 1, (Color){200,200,220,255});

    DrawTextInput(&name_input,   font);
    DrawTextInput(&start_input,  font);
    DrawTextInput(&end_input,    font);
    DrawTextInput(&desc_input,   font);

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
    int len = (int)fminf(count, strlen(text));
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

    // ───── Click to focus + cursor placement ─────
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        bool was_active = ti->active;
        ti->active = CheckCollisionPointRec(mouse, ti->rect);

        if (ti->active && !was_active) {
            float rel_x = mouse.x - (ti->rect.x + 12);
            if (rel_x < 0) {
                ti->cursor_pos = 0;
            } else {
                int low = 0, high = (int)strlen(ti->text);
                while (low < high) {
                    int mid = (low + high + 1) / 2;
                    if (TextWidthUpTo(font, ti->text, mid, 20, 1) <= rel_x)
                        low = mid;
                    else
                        high = mid - 1;
                }
                ti->cursor_pos = low;
            }
        }
    }

    if (!ti->active) return;

    // ───── Character input (same as before) ─────
    int key = GetCharPressed();
    while (key > 0) {
        if (key >= 32 && key <= 125 && strlen(ti->text) < MAX_INPUT-2) {
            memmove(ti->text + ti->cursor_pos + 1,
                    ti->text + ti->cursor_pos,
                    strlen(ti->text + ti->cursor_pos) + 1);
            ti->text[ti->cursor_pos++] = (char)key;
        }
        key = GetCharPressed();
    }

    // ───── KEY REPEAT SYSTEM (Backspace & Delete) ─────
    static float repeatTimer = 0.0f;
    static int   repeatKey   = 0;

    const float REPEAT_DELAY    = 0.45f;  // seconds before repeat starts
    const float REPEAT_INTERVAL = 0.04f;  // how fast it repeats after delay

    int currentKey = 0;
    if (IsKeyDown(KEY_BACKSPACE)) currentKey = KEY_BACKSPACE;
    else if (IsKeyDown(KEY_DELETE))   currentKey = KEY_DELETE;

    if (currentKey != 0) {
        if (currentKey != repeatKey) {
            // Key just pressed — delete one char immediately
            if (currentKey == KEY_BACKSPACE && ti->cursor_pos > 0) {
                memmove(ti->text + ti->cursor_pos - 1,
                        ti->text + ti->cursor_pos,
                        strlen(ti->text + ti->cursor_pos) + 1);
                ti->cursor_pos--;
            }
            else if (currentKey == KEY_DELETE && ti->cursor_pos < (int)strlen(ti->text)) {
                memmove(ti->text + ti->cursor_pos,
                        ti->text + ti->cursor_pos + 1,
                        strlen(ti->text + ti->cursor_pos + 1) + 1);
            }
            repeatKey = currentKey;
            repeatTimer = REPEAT_DELAY;  // wait before repeating
        } else {
            // Key is held — handle repeat timing
            repeatTimer -= GetFrameTime();
            if (repeatTimer <= 0.0f) {
                if (currentKey == KEY_BACKSPACE && ti->cursor_pos > 0) {
                    memmove(ti->text + ti->cursor_pos - 1,
                            ti->text + ti->cursor_pos,
                            strlen(ti->text + ti->cursor_pos) + 1);
                    ti->cursor_pos--;
                }
                else if (currentKey == KEY_DELETE && ti->cursor_pos < (int)strlen(ti->text)) {
                    memmove(ti->text + ti->cursor_pos,
                            ti->text + ti->cursor_pos + 1,
                            strlen(ti->text + ti->cursor_pos + 1) + 1);
                }
                repeatTimer = REPEAT_INTERVAL;  // next repeat soon
            }
        }
    } else {
        // No repeat key held
        repeatKey = 0;
        repeatTimer = 0.0f;
    }

    // ───── Cursor movement keys (also repeat ─────
    if (IsKeyPressedRepeat(KEY_LEFT) && ti->cursor_pos > 0) ti->cursor_pos--;
    if (IsKeyPressedRepeat(KEY_RIGHT) && ti->cursor_pos < (int)strlen(ti->text)) ti->cursor_pos++;
    if (IsKeyPressed(KEY_HOME)) ti->cursor_pos = 0;
    if (IsKeyPressed(KEY_END)) ti->cursor_pos = (int)strlen(ti->text);

    // ───── Ctrl+V Paste (unchanged) ─────
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V)) {
        const char *clipboard = GetClipboardText();
        if (clipboard) {
            size_t len = strlen(clipboard);
            size_t space = MAX_INPUT - 1 - strlen(ti->text);
            if (space > 0) {
                if (len > space) len = space;
                memmove(ti->text + ti->cursor_pos + len,
                        ti->text + ti->cursor_pos,
                        strlen(ti->text + ti->cursor_pos) + 1);
                memcpy(ti->text + ti->cursor_pos, clipboard, len);
                ti->cursor_pos += (int)len;
                ti->text[MAX_INPUT-1] = '\0';
            }
        }
    }
}

int main(void) {
    const int W = 1500, H = 900;

    InitWindow(W, H, "Lifetime Visual Time Tracker");
    SetTargetFPS(60);

    // ────────────────────── YOUR ORIGINAL FONT (exactly as you had it) ──────────────────────
    font = LoadFontEx("/usr/share/fonts/TTF/DejaVuSans.ttf", 22, NULL, 0);
    if (font.texture.id == 0) font = GetFontDefault();

    // Keep text smooth & crisp
    SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR);
    // ──────────────────────────────────────────────────────────────────────────────────────

    LoadTracker("timetracker.json");
    tracker.pixels_per_year = 700.0f;

    // ───── CENTER TODAY ON SCREEN (your original logic — untouched) ─────
    time_t now = time(NULL);
    struct tm today_tm = {0};
    localtime_r(&now, &today_tm);
    today_tm.tm_hour = today_tm.tm_min = today_tm.tm_sec = 0;
    today_tm.tm_isdst = -1;
    time_t today_midnight = mktime(&today_tm);

    double visible_pixels = GetScreenWidth() - 150.0;
    double visible_seconds = visible_pixels * (365.25 * 86400.0) / tracker.pixels_per_year;
    tracker.view_start = today_midnight - (time_t)(visible_seconds / 2.0);

    time_t min_view = today_midnight - 200LL * 365 * 86400;
    time_t max_view = today_midnight + 100LL * 365 * 86400;
    if (tracker.view_start < min_view) tracker.view_start = min_view;
    if (tracker.view_start > max_view) tracker.view_start = max_view;

    char today_str[32];
    strftime(today_str, sizeof(today_str), "%Y-%m-%d", localtime(&now));

    InitTextInput(&name_input,  (Rectangle){180, 20, 420, 48}, "");
    InitTextInput(&start_input, (Rectangle){680, 20, 200, 48}, today_str);
    InitTextInput(&end_input,   (Rectangle){960, 20, 200, 48}, today_str);
    InitTextInput(&desc_input, (Rectangle){180, 80, 980, 48}, "");

    while (!WindowShouldClose()) {
        HandlePanningAndZooming();
        HandleSelectionAndDragging();
        UpdateTextInput(&name_input, font);
        UpdateTextInput(&start_input, font);
        UpdateTextInput(&end_input, font);
        UpdateTextInput(&desc_input, font);
        HandleKeyboardShortcuts();

        if (selected != last_selected) {
            SyncInputsToSelected();
            last_selected = selected;
        }
        if (selected >= 0) {
            static char last_name[128] = "", last_start[128] = "", last_end[128] = "";
            if (strcmp(name_input.text, last_name) ||
                strcmp(start_input.text, last_start) ||
                strcmp(end_input.text, last_end)) {
                ApplyInputsToSelected();
                strcpy(last_name,  name_input.text);
                strcpy(last_start, start_input.text);
                strcpy(last_end,   end_input.text);
            }
        }

BeginDrawing();
    ClearBackground((Color){12, 12, 28, 255});

    // ←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←
    // 1. Draw UI FIRST (so year labels can appear on top)
    DrawUI();                     // ← moved up here
    // 2. Then draw the timeline (year labels will be on top)
    BeginScissorMode(0, (int)timeline_y, GetScreenWidth(), GetScreenHeight() - (int)timeline_y);
               // year labels now on top of everything
        DrawEvents();
    EndScissorMode();
DrawTimelineGrid();
    // 3. FPS stays last (always visible)
DrawGlobalTooltip();
    DrawFPS(10, 10);
EndDrawing();
    }

    SaveTracker("timetracker.json");
    UnloadFont(font);
    CloseWindow();
    return 0;
}
