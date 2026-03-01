#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "cJSON.h"

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#include "nuklear.h"

#define CARDS_PER_PAGE 12
#define MAX_CARDS_PER_PAGE 12
#define WIN_W 1840
#define WIN_H 920

#define COL_BG         nk_rgb(10, 10, 10)
#define COL_CARD       nk_rgb(25, 25, 25)
#define COL_ACCENT     nk_rgb(59, 130, 246)
#define COL_SUCCESS    nk_rgb(34, 197, 94)
#define COL_DANGER     nk_rgb(239, 68, 68)
#define COL_MUTED      nk_rgb(120, 120, 120)
#define COL_DISABLED   nk_rgb(80, 80, 80)

typedef struct {
    char word[128], filename[128], correct_trans[128],
         all_trans[512], definition[1024], info[1024];
} WordData;

typedef struct {
    int id;
    char title[128], description[256];
    WordData *words;
    int word_count;
} LevelData;

typedef struct nk_gdi_font {
    struct nk_user_font nk;
    HFONT handle;
} nk_gdi_font;

LevelData *en_levels = NULL, *de_levels = NULL;
int total_en_levels = 0, total_de_levels = 0;
LevelData *current_levels = NULL;
int total_current_levels = 0;
int current_page = 0;

enum AppState { LANDING, QUIZ, RESULT } current_state = LANDING;

int active_lvl_idx = 0;
int current_word_idx = 0;
int score = 0, mistakes = 0;
int is_revealed = 0, is_correct = 0;
char options[4][128];
int correct_option_idx = 0;
char current_lang[3] = "en";

nk_gdi_font font_s, font_m, font_l, font_m2, font_xs;

/* ---- Language texts ---- */
const char* get_text(const char* en_text) {
    if (strcmp(current_lang, "de") == 0) {
        struct { const char* en; const char* de; } texts[] = {
            {"ONDA SFASATA DESKTOP", "ONDA SFASATA DESKTOP"},
            {"English", "English"},
            {"Deutsch", "Deutsch"},
            {"PREVIOUS PAGE", "VORHERIGE SEITE"},
            {"NEXT PAGE", "NACHTSTE SEITE"},
            {"START", "STARTEN"},
            {"QUIT", "BEENDEN"},
            {"LISTEN", "ANHOREN"},
            {"NEXT", "WEITER"},
            {"CORRECT", "RICHTIG"},
            {"INCORRECT", "FALSCH"},
            {"Meaning:", "Bedeutung:"},
            {"Info:", "Info:"},
            {"LEVEL COMPLETE!", "STUFE ABGESCHLOSSEN!"},
            {"CONTINUE", "WEITER"},
            {"Level %d", "Stufe %d"}
        };
        for (int i = 0; i < (int)(sizeof(texts)/sizeof(texts[0])); i++) {
            if (strcmp(en_text, texts[i].en) == 0) return texts[i].de;
        }
        return en_text;
    }
    return en_text;
}

/* ---- Config path helpers ---- */
static void get_exe_dir(char *buf, size_t bufSize) {
    DWORD len = GetModuleFileNameA(NULL, buf, (DWORD)bufSize);
    if (len == 0 || len == bufSize) {
        buf[0] = '\0';
        return;
    }
    for (int i = (int)len - 1; i >= 0; --i) {
        if (buf[i] == '\\' || buf[i] == '/') {
            buf[i] = '\0';
            break;
        }
    }
}

static void get_config_path(char *buf, size_t bufSize) {
    char dir[MAX_PATH];
    get_exe_dir(dir, sizeof(dir));
    snprintf(buf, bufSize, "%s\\config.txt", dir);
}

void load_language_from_config() {
    char path[MAX_PATH];
    get_config_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) {
        strcpy(current_lang, "en");
        return;
    }
    char line[64] = {0};
    if (fgets(line, sizeof(line), f)) {
        char *p = strstr(line, "lang=");
        if (p) {
            p += 5;
            if (strncmp(p, "de", 2) == 0) strcpy(current_lang, "de");
            else strcpy(current_lang, "en");
        }
    }
    fclose(f);
}

void save_language_to_config() {
    char path[MAX_PATH];
    get_config_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "lang=%s\n", current_lang);
    fclose(f);
}

/* ---- Audio: simple file-based, like your working version ---- */
void play_sound_native(const char* name, int is_word) {
    char path[MAX_PATH];
    if (is_word)
        sprintf(path, "audio\\Italian\\%s.wav", name);
    else
        sprintf(path, "audio\\%s.wav", name);
    PlaySoundA(path, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

/* ---- UTF-8 text rendering ---- */
void DrawTextUTF8(HDC hdc, const char* text, int len, RECT* r, UINT flags, HFONT hFont) {
    if (!text || len == 0) return;
    SelectObject(hdc, hFont);
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, len, NULL, 0);
    wchar_t* wbuf = (wchar_t*)malloc(sizeof(wchar_t) * (wlen + 1));
    MultiByteToWideChar(CP_UTF8, 0, text, len, wbuf, wlen);
    wbuf[wlen] = L'\0';
    DrawTextW(hdc, wbuf, wlen, r, flags | DT_NOCLIP | DT_NOPREFIX);
    free(wbuf);
}

float gdi_w(nk_handle h, float height, const char *s, int len) {
    SIZE z;
    HDC hdc = GetDC(NULL);
    struct nk_gdi_font *f = (struct nk_gdi_font*)h.ptr;
    SelectObject(hdc, f->handle);
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s, len, NULL, 0);
    wchar_t* wbuf = (wchar_t*)malloc(sizeof(wchar_t) * (wlen + 1));
    MultiByteToWideChar(CP_UTF8, 0, s, len, wbuf, wlen);
    GetTextExtentPoint32W(hdc, wbuf, wlen, &z);
    free(wbuf);
    ReleaseDC(NULL, hdc);
    return (float)z.cx + 2.0f;
}

/* ---- Level management ---- */
void free_levels(LevelData **levels, int *total) {
    if (!*levels) return;
    for (int i = 0; i < *total; ++i) {
        if ((*levels)[i].words) free((*levels)[i].words);
    }
    free(*levels);
    *levels = NULL;
    *total = 0;
}

void switch_to_current_levels() {
    if (strcmp(current_lang, "en") == 0) {
        current_levels = en_levels;
        total_current_levels = total_en_levels;
    } else {
        current_levels = de_levels;
        total_current_levels = total_de_levels;
    }
}

void load_levels_from_json(LevelData **levels, int *total, const char *file) {
    FILE *f = fopen(file, "rb");
    if (!f) {
        MessageBoxA(NULL, file, "Missing JSON", MB_ICONERROR);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = (char*)malloc(len + 1);
    fread(data, 1, len, f);
    fclose(f);
    data[len] = 0;

    cJSON *json = cJSON_Parse(data);
    if (!json) {
        MessageBoxA(NULL, "JSON parse error", "Error", MB_ICONERROR);
        free(data);
        exit(1);
    }

    cJSON *levels_array = cJSON_GetObjectItem(json, "levels");
    *total = cJSON_GetArraySize(levels_array);
    *levels = (LevelData*)malloc(sizeof(LevelData) * (*total));

    for (int i = 0; i < *total; i++) {
        cJSON *lvl = cJSON_GetArrayItem(levels_array, i);
        (*levels)[i].id = cJSON_GetObjectItem(lvl, "id")->valueint;
        strcpy((*levels)[i].title, cJSON_GetObjectItem(lvl, "title")->valuestring);

        cJSON *words_array = cJSON_GetObjectItem(lvl, "words");
        (*levels)[i].word_count = cJSON_GetArraySize(words_array);
        (*levels)[i].words = (WordData*)malloc(sizeof(WordData) * (*levels)[i].word_count);

        for (int j = 0; j < (*levels)[i].word_count; j++) {
            cJSON *w = cJSON_GetArrayItem(words_array, j);
            WordData *wd = &(*levels)[i].words[j];

            strcpy(wd->word,      cJSON_GetObjectItem(w, "word")->valuestring);
            strcpy(wd->filename,  cJSON_GetObjectItem(w, "filename")->valuestring);
            strcpy(wd->definition,cJSON_GetObjectItem(w, "definition")->valuestring);

            cJSON *info = cJSON_GetObjectItem(w, "info");
            if (info) strcpy(wd->info, info->valuestring);
            else wd->info[0] = '\0';

            cJSON *trans_arr = cJSON_GetObjectItem(w, "translations");
            strcpy(wd->correct_trans, cJSON_GetArrayItem(trans_arr, 0)->valuestring);

            wd->all_trans[0] = '\0';
            for (int k = 0; k < cJSON_GetArraySize(trans_arr); k++) {
                strcat(wd->all_trans, cJSON_GetArrayItem(trans_arr, k)->valuestring);
                if (k < cJSON_GetArraySize(trans_arr) - 1) strcat(wd->all_trans, ", ");
            }
        }
    }

    cJSON_Delete(json);
    free(data);
}

void load_all_levels() {
    free_levels(&en_levels, &total_en_levels);
    free_levels(&de_levels, &total_de_levels);
    load_levels_from_json(&en_levels, &total_en_levels, "en_levels.json");
    load_levels_from_json(&de_levels, &total_de_levels, "de_levels.json");
    switch_to_current_levels();
}

/* ---- Quiz ---- */
void setup_quiz_question() {
    LevelData *lvl = &current_levels[active_lvl_idx];
    WordData *w = &lvl->words[current_word_idx];

    correct_option_idx = rand() % 4;
    for (int i = 0; i < 4; i++) {
        if (i == correct_option_idx)
            strcpy(options[i], w->correct_trans);
        else
            strcpy(options[i],
                   current_levels[rand()%total_current_levels].words[0].correct_trans);
    }
    play_sound_native(w->filename, 1);
}

/* ---- Win32 ---- */
LRESULT CALLBACK WindowProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

/* ---- Main ---- */
int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    srand((unsigned int)time(NULL));

    load_language_from_config();
    load_all_levels();

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance   = inst;
    wc.hCursor     = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "OndaClass";
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, "OndaClass", "Onda Sfasata Desktop",
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               10, 10, WIN_W, WIN_H,
                               0, 0, inst, 0);
    HDC hdc = GetDC(hwnd);

    // Fonts
    font_xs.handle = CreateFontA(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                 DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    font_s.handle  = CreateFontA(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                 DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    font_m.handle  = CreateFontA(-24, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
                                 DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    font_l.handle  = CreateFontA(-54, 0, 0, 0, FW_BOLD, 0, 0, 0,
                                 DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    font_m2.handle = CreateFontA(-20, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
                                 DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

    font_xs.nk.userdata = nk_handle_ptr(&font_xs);
    font_xs.nk.height   = 15;
    font_xs.nk.width    = gdi_w;

    font_s.nk.userdata  = nk_handle_ptr(&font_s);
    font_s.nk.height    = 16;
    font_s.nk.width     = gdi_w;

    font_m.nk.userdata  = nk_handle_ptr(&font_m);
    font_m.nk.height    = 24;
    font_m.nk.width     = gdi_w;

    font_l.nk.userdata  = nk_handle_ptr(&font_l);
    font_l.nk.height    = 54;
    font_l.nk.width     = gdi_w;

    font_m2.nk.userdata = nk_handle_ptr(&font_m2);
    font_m2.nk.height   = 20;
    font_m2.nk.width    = gdi_w;

    struct nk_context ctx;
    nk_init_default(&ctx, &font_m.nk);

    struct nk_color table[NK_COLOR_COUNT];
    for (int i = 0; i < NK_COLOR_COUNT; ++i)
        table[i] = nk_rgba(40,40,40,255);
    table[NK_COLOR_WINDOW]        = nk_rgb(0,0,0);
    table[NK_COLOR_TEXT]          = nk_rgb(255,255,255);
    table[NK_COLOR_BUTTON]        = COL_CARD;
    table[NK_COLOR_BUTTON_HOVER]  = nk_rgb(50,50,50);
    table[NK_COLOR_BUTTON_ACTIVE] = COL_ACCENT;
    nk_style_from_table(&ctx, table);

    while (1) {
        MSG msg;
        nk_input_begin(&ctx);
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto cleanup;
            if (msg.message == WM_LBUTTONDOWN)
                nk_input_button(&ctx, NK_BUTTON_LEFT,
                                GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam), 1);
            if (msg.message == WM_LBUTTONUP)
                nk_input_button(&ctx, NK_BUTTON_LEFT,
                                GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam), 0);
            if (msg.message == WM_MOUSEMOVE)
                nk_input_motion(&ctx,
                                GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam));
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        nk_input_end(&ctx);

        RECT rc;
        GetClientRect(hwnd, &rc);
        float win_width = (float)rc.right;

        if (nk_begin(&ctx, "Main", nk_rect(0, 0, win_width, (float)rc.bottom),
                     NK_WINDOW_NO_SCROLLBAR)) {

            // Title + language toggle
            if (current_state == LANDING) {
                nk_layout_row_begin(&ctx, NK_STATIC, 60, 3);
                nk_layout_row_push(&ctx, 280.0f);
                nk_spacing(&ctx, 1);

                float title_width = win_width - 560.0f;
                nk_layout_row_push(&ctx, title_width);
                nk_style_push_font(&ctx, &font_m2.nk);
                nk_label(&ctx, get_text("ONDA SFASATA DESKTOP"), NK_TEXT_CENTERED);
                nk_style_pop_font(&ctx);

                nk_layout_row_push(&ctx, 280.0f);
                char lang_label[32];
                strcpy(lang_label, get_text(strcmp(current_lang, "de") == 0
                                            ? "English" : "Deutsch"));
                if (nk_button_label(&ctx, lang_label)) {
                    if (strcmp(current_lang, "de") == 0) strcpy(current_lang, "en");
                    else strcpy(current_lang, "de");
                    save_language_to_config();
                    current_state = LANDING;
                    current_page = 0;
                    active_lvl_idx = 0;
                    current_word_idx = 0;
                    score = mistakes = 0;
                    is_revealed = is_correct = 0;
                    switch_to_current_levels();
                }
                nk_layout_row_end(&ctx);
            }

            nk_layout_row_dynamic(&ctx, 1, 1);
            nk_spacing(&ctx, 1);

            if (current_state == LANDING) {
                float btn_width = 280.0f * 1.1f;

                nk_layout_row_begin(&ctx, NK_STATIC, 60, 3);

                nk_layout_row_push(&ctx, btn_width);
                ctx.style.text.color = current_page > 0 ? nk_rgb(255,255,255) : COL_DISABLED;
                if (current_page > 0) {
                    if (nk_button_label(&ctx, get_text("PREVIOUS PAGE")))
                        current_page--;
                } else {
                    nk_label(&ctx, get_text("PREVIOUS PAGE"), NK_TEXT_CENTERED);
                }
                ctx.style.text.color = nk_rgb(255,255,255);

                float page_info_width = win_width - (btn_width * 2);
                nk_layout_row_push(&ctx, page_info_width);
                char p_buf[64];
                int end_idx = (current_page + 1) * MAX_CARDS_PER_PAGE;
                if (end_idx > total_current_levels) end_idx = total_current_levels;
                sprintf(p_buf, "%s %d - %d",
                        strcmp(current_lang,"de")==0 ? "Stufen" : "Levels",
                        (current_page * MAX_CARDS_PER_PAGE) + 1, end_idx);
                nk_style_push_font(&ctx, &font_xs.nk);
                nk_label(&ctx, p_buf, NK_TEXT_CENTERED);
                nk_style_pop_font(&ctx);

                nk_layout_row_push(&ctx, btn_width);
                ctx.style.text.color =
                    ((current_page + 1) * MAX_CARDS_PER_PAGE < total_current_levels)
                    ? nk_rgb(255,255,255) : COL_DISABLED;
                if ((current_page + 1) * MAX_CARDS_PER_PAGE < total_current_levels) {
                    if (nk_button_label(&ctx, get_text("NEXT PAGE")))
                        current_page++;
                } else {
                    nk_label(&ctx, get_text("NEXT PAGE"), NK_TEXT_CENTERED);
                }
                ctx.style.text.color = nk_rgb(255,255,255);
                nk_layout_row_end(&ctx);

                nk_layout_row_dynamic(&ctx, 22, 1);
                nk_spacing(&ctx, 1);

                int start_idx = current_page * MAX_CARDS_PER_PAGE;
                int cards_this_page = total_current_levels - start_idx;
                if (cards_this_page > MAX_CARDS_PER_PAGE)
                    cards_this_page = MAX_CARDS_PER_PAGE;

                int rows = (cards_this_page + 1) / 2;
                for (int row = 0; row < rows; row++) {
                    nk_layout_row_dynamic(&ctx, 130, 2);
                    for (int col = 0; col < 2; col++) {
                        int idx = start_idx + (row * 2) + col;
                        if (idx < total_current_levels &&
                            idx < (current_page * MAX_CARDS_PER_PAGE) + MAX_CARDS_PER_PAGE) {
                            if (nk_group_begin(&ctx, current_levels[idx].title,
                                               NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
                                nk_style_push_font(&ctx, &font_s.nk);
                                ctx.style.text.color = COL_MUTED;
                                char id_b[32];
                                sprintf(id_b, get_text("Level %d"),
                                        current_levels[idx].id);
                                nk_layout_row_dynamic(&ctx, 30, 1);
                                nk_label(&ctx, id_b, NK_TEXT_LEFT);
                                nk_style_pop_font(&ctx);

                                ctx.style.text.color = nk_rgb(255,255,255);
                                nk_layout_row_dynamic(&ctx, 35, 1);
                                nk_label(&ctx, current_levels[idx].title, NK_TEXT_LEFT);

                                nk_layout_row_dynamic(&ctx, 43, 1);
                                if (nk_button_label(&ctx, get_text("START"))) {
                                    active_lvl_idx = idx;
                                    current_word_idx = 0;
                                    score = 0;
                                    mistakes = 0;
                                    current_state = QUIZ;
                                    is_revealed = 0;
                                    setup_quiz_question();
                                }
                                nk_group_end(&ctx);
                            }
                        } else {
                            nk_spacing(&ctx, 1);
                        }
                    }
                }
            } else if (current_state == QUIZ) {
                LevelData *lvl = &current_levels[active_lvl_idx];
                WordData *w = &lvl->words[current_word_idx];

                nk_layout_row_dynamic(&ctx, 45, 3);
                if (nk_button_label(&ctx, get_text("QUIT")))
                    current_state = LANDING;

                char stat[64];
                sprintf(stat, "%s %d / %d",
                        strcmp(current_lang,"de")==0 ? "Wort" : "Word",
                        current_word_idx + 1, lvl->word_count);
                nk_style_push_font(&ctx, &font_xs.nk);
                nk_label(&ctx, stat, NK_TEXT_CENTERED);
                nk_style_pop_font(&ctx);

                char sc[32];
                sprintf(sc, "%s: %d",
                        strcmp(current_lang,"de")==0 ? "Punkte" : "Score", score);
                nk_style_push_font(&ctx, &font_xs.nk);
                nk_label(&ctx, sc, NK_TEXT_RIGHT);
                nk_style_pop_font(&ctx);

                nk_layout_row_dynamic(&ctx, 120, 1);
                nk_style_push_font(&ctx, &font_l.nk);
                ctx.style.text.color = COL_ACCENT;
                nk_label(&ctx, w->word, NK_TEXT_CENTERED);
                ctx.style.text.color = nk_rgb(255,255,255);
                nk_style_pop_font(&ctx);

                nk_layout_row_dynamic(&ctx, 50, 3);
                nk_spacing(&ctx, 1);
                if (nk_button_label(&ctx, get_text("LISTEN")))
                    play_sound_native(w->filename, 1);
                nk_spacing(&ctx, 1);

                nk_layout_row_dynamic(&ctx, 75, 2);
                for (int i = 0; i < 4; i++) {
                    if (nk_button_label(&ctx, options[i]) && !is_revealed) {
                        is_revealed = 1;
                        if (i == correct_option_idx) {
                            score++;
                            is_correct = 1;
                            play_sound_native("success", 0);
                        } else {
                            mistakes++;
                            is_correct = 0;
                            play_sound_native("error", 0);
                        }
                    }
                }

                if (is_revealed) {
                    nk_layout_row_dynamic(&ctx, 300, 1);
                    if (nk_group_begin(&ctx, get_text("Info:"), NK_WINDOW_BORDER)) {
                        nk_layout_row_dynamic(&ctx, 35, 1);
                        ctx.style.text.color = is_correct ? COL_SUCCESS : COL_DANGER;
                        nk_label(&ctx, get_text(is_correct ? "CORRECT" : "INCORRECT"),
                                 NK_TEXT_LEFT);
                        ctx.style.text.color = nk_rgb(255,255,255);

                        nk_layout_row_dynamic(&ctx, 30, 1);
                        char trans[256];
                        sprintf(trans, "%s %s", get_text("Meaning:"), w->all_trans);
                        nk_label(&ctx, trans, NK_TEXT_LEFT);

                        nk_layout_row_dynamic(&ctx, 120, 1);
                        nk_label_wrap(&ctx, w->definition);

                        if (w->info[0]) {
                            nk_layout_row_dynamic(&ctx, 70, 1);
                            ctx.style.text.color = COL_MUTED;
                            nk_label_wrap(&ctx, w->info);
                            ctx.style.text.color = nk_rgb(255,255,255);
                        }
                        nk_group_end(&ctx);
                    }

                    nk_layout_row_dynamic(&ctx, 60, 1);
                    if (nk_button_label(&ctx, get_text("NEXT"))) {
                        is_revealed = 0;
                        if (current_word_idx < lvl->word_count - 1) {
                            current_word_idx++;
                            setup_quiz_question();
                        } else {
                            current_state = RESULT;
                            play_sound_native("completed", 0);
                        }
                    }
                }
            } else if (current_state == RESULT) {
                nk_layout_row_dynamic(&ctx, 150, 1);
                nk_style_push_font(&ctx, &font_l.nk);
                nk_label(&ctx, get_text("LEVEL COMPLETE!"), NK_TEXT_CENTERED);
                nk_style_pop_font(&ctx);

                char res[128];
                sprintf(res, "%s: %d | %s: %d",
                        strcmp(current_lang,"de")==0 ? "Endpunkte" : "Final Score", score,
                        strcmp(current_lang,"de")==0 ? "Fehler" : "Mistakes", mistakes);
                nk_layout_row_dynamic(&ctx, 50, 1);
                nk_style_push_font(&ctx, &font_xs.nk);
                nk_label(&ctx, res, NK_TEXT_CENTERED);
                nk_style_pop_font(&ctx);

                nk_layout_row_dynamic(&ctx, 60, 1);
                if (nk_button_label(&ctx, get_text("CONTINUE")))
                    current_state = LANDING;
            }
        }
        nk_end(&ctx);

        // Render
        HDC mDC = CreateCompatibleDC(hdc);
        HBITMAP mBM = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        SelectObject(mDC, mBM);
        HBRUSH bg = CreateSolidBrush(RGB(10,10,10));
        FillRect(mDC, &rc, bg);
        DeleteObject(bg);

        const struct nk_command *cmd;
        nk_foreach(cmd, &ctx) {
            switch (cmd->type) {
                case NK_COMMAND_RECT_FILLED: {
                    const struct nk_command_rect_filled *r =
                        (const struct nk_command_rect_filled*)cmd;
                    HBRUSH br =
                        CreateSolidBrush(RGB(r->color.r, r->color.g, r->color.b));
                    RECT gr = {(int)r->x, (int)r->y,
                               (int)(r->x+r->w), (int)(r->y+r->h)};
                    FillRect(mDC, &gr, br);
                    DeleteObject(br);
                } break;

                case NK_COMMAND_TEXT: {
                    const struct nk_command_text *t =
                        (const struct nk_command_text*)cmd;
                    SetBkMode(mDC, TRANSPARENT);
                    SetTextColor(mDC,
                                 RGB(t->foreground.r, t->foreground.g, t->foreground.b));
                    RECT tr = {(int)t->x, (int)t->y,
                               (int)(t->x + t->w), (int)(t->y + t->h)};

                    // Lower only START / STARTEN labels by 2px
                    char buf[64] = {0};
                    int copy_len = t->length < 63 ? (int)t->length : 63;
                    memcpy(buf, t->string, copy_len);
                    buf[copy_len] = '\0';
                    if (strstr(buf, "START") || strstr(buf, "STARTEN")) {
                        tr.top += 2;
                        tr.bottom += 2;
                    }

                    UINT align = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
                    if (t->h > 65) align = DT_LEFT | DT_WORDBREAK;

                    DrawTextUTF8(mDC, (const char*)t->string, (int)t->length,
                                 &tr, align,
                                 ((struct nk_gdi_font*)t->font->userdata.ptr)->handle);
                } break;

                case NK_COMMAND_RECT: {
                    const struct nk_command_rect *r =
                        (const struct nk_command_rect*)cmd;
                    HPEN p =
                        CreatePen(PS_SOLID, 1, RGB(r->color.r, r->color.g, r->color.b));
                    SelectObject(mDC, p);
                    SelectObject(mDC, GetStockObject(NULL_BRUSH));
                    Rectangle(mDC, (int)r->x, (int)r->y,
                              (int)(r->x+r->w), (int)(r->y+r->h));
                    DeleteObject(p);
                } break;

                default: break;
            }
        }

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mDC, 0, 0, SRCCOPY);
        DeleteObject(mBM);
        DeleteDC(mDC);
        nk_clear(&ctx);
        Sleep(16);
    }

cleanup:
    free_levels(&en_levels, &total_en_levels);
    free_levels(&de_levels, &total_de_levels);
    DeleteObject(font_xs.handle);
    DeleteObject(font_s.handle);
    DeleteObject(font_m.handle);
    DeleteObject(font_l.handle);
    DeleteObject(font_m2.handle);
    ReleaseDC(hwnd, hdc);
    return 0;
}
