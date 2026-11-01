/* Offline cleanup of the game's quest scripts.
 *
 * The pop-ups for offers, ads, ratings, Facebook, news and the raids'
 * recruiting are not game code but quests: XML in assets/quests.xml and the files it attaches
 * from assets/quest_extensions/ (docs/RE_NOTES.md, "Quest scripts"). Each
 * file is edited as the game reads it, by the table below:
 *
 *   Q_OFF  the quest gets a condition that never holds. Its name stays, so
 *          saved progress, ClearQuestQueue and resumes still find it.
 *   Q_CUT  removes the elements of one quest that start with the given
 *          text (count must match exactly), e.g. one dialog of several.
 *
 * Every edit checks what it changes, like the code patches: a quest that
 * is missing, or a cut whose count differs, is logged and skipped. Edits
 * already present (the game rewrites its cached copies with what it read)
 * are recognised and left as they are.
 *
 * The files are guarded twice. Each has a .hash, MD5 of its relative path,
 * contents and the device ID (as packs.xml), checked when it is loaded:
 * reading <file>.hash gets the hash of the edited contents, but only after
 * the original hash was confirmed to follow that scheme, so an unknown
 * scheme leaves the file alone. Each also has an RSA signature (.sha),
 * which patches.c turns off (0x4a6a3af4).
 */
#include "s3e/s3e_internal.h"
#include "s3e/md5.h"
#include "platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { Q_OFF, Q_CUT };

typedef struct {
    const char *file;     /* relative path, as in the .hash */
    const char *quest;
    int op;
    const char *start;    /* Q_CUT: start of the elements to remove */
    int count;            /* Q_CUT: how many the quest has */
} q_edit;

#define QUESTS     "assets/quests.xml"
#define PROMOS     "assets/quest_extensions/promotions.xml"
#define RAIDS      "assets/quest_extensions/raid_quests.xml"
#define ZONE2      "assets/quest_extensions/zone_2/story.xml"
#define ZONE7      "assets/quest_extensions/zone_7/story.xml"
#define OFF(f, q)  { f, q, Q_OFF, NULL, 0 }
#define CUT(f, q, s, n) { f, q, Q_CUT, s, n }

/* Only messages that pop up on their own. Whatever answers a button the
 * player presses (Facebook login, "GET FREE", an offer's map button...) is
 * left as it was, so pressing it shows the game's own "no connection". */
static const q_edit s_edits[] = {
    /* Facebook: "log in for 5 gems" after fights, and the share prompt
     * after a level up. */
    OFF(QUESTS, "FBFirstTimeOffer"), OFF(QUESTS, "FBFirstTimeShareReminder"), OFF(QUESTS, "FBShare"),
    /* Ads after fights (MoPub, and the in-house "buy anything to remove
     * ads"), "rate us", news, and the reminders about the free-gems tab. */
    OFF(QUESTS, "RollMoPubAd"), OFF(QUESTS, "ShowAd"), OFF(QUESTS, "ShowOurAd"),
    OFF(QUESTS, "RatingOfferChina"), OFF(QUESTS, "RatingOffer"), OFF(QUESTS, "ShowNews"),
    OFF(QUESTS, "FreeReminderOnTabLeave"), OFF(QUESTS, "FreeReminder"),
    /* Real-money offers that open by themselves: the starter pack after a
     * tournament and at start-up, "buy coins" in Survival, "new currency
     * pack". Their map buttons, if any, still work. */
    OFF(QUESTS, "StarterPackLaunch"), OFF(QUESTS, "StarterPackOnRestart"),
    OFF(QUESTS, "SurvivalPayReminder"), OFF(QUESTS, "currency_alert"),

    /* Real-money "special offers" that open by themselves (fight start,
     * session start, restart, buying gems). */
    OFF(PROMOS, "promo_FirstPayment_OnFightEnter"), OFF(PROMOS, "promo_FirstPayment_OnRestart"),
    OFF(PROMOS, "promo_RepeatPayment_SessionStart_Gold"), OFF(PROMOS, "promo_RepeatPayment_SessionStart_Ruby"),
    OFF(PROMOS, "promo_RepeatPayment_OnRestart"), OFF(PROMOS, "promo_IncreasePayment_OnPurchase"),
    OFF(PROMOS, "promo_IncreasePayment_OnRestart"),

    /* Raids (online multiplayer): the intro after Lynx, the raid shop
     * notice, and "join the raids" every ten fights. */
    OFF(RAIDS, "RaidTutorialLauncherFightEnd"), OFF(RAIDS, "RaidShopNotification"),
    OFF(RAIDS, "GoToRaidsDialog"),
    /* The puppeteer's "join the raid forces" after an Ascension loss. */
    OFF(ZONE2, "Zone2LossTutor"),
    /* After Titan: the ending, credits, sphere and May's line stay; the
     * sensei's two raid hooks and the raid button tutorial go. */
    CUT(ZONE7, "Zone7TitanDefeated_NotInEclipseMode",
        "<Dialog Type=\"Regular\" Title=\"characterSensei\" Image=\"character_sensei\"", 2),
    CUT(ZONE7, "Zone7TitanDefeated_NotInEclipseMode", "<RaidsButtonTutorial", 1),
    CUT(ZONE7, "Zone7TitanDefeated_InEclipseMode",
        "<Dialog Type=\"Regular\" Title=\"characterSensei\" Image=\"character_sensei\"", 2),
    CUT(ZONE7, "Zone7TitanDefeated_InEclipseMode", "<RaidsButtonTutorial", 1),
};
#define N_EDITS (sizeof(s_edits) / sizeof(s_edits[0]))

/* A string literal no quest variable ever equals. */
#define NEVER "<Equal Value1=\"vita_offline\" Value2=\"1\" />"

/* --- text helpers ------------------------------------------------------ */

typedef struct { char *p; size_t len; } text;

static const char *find(const char *s, const char *end, const char *needle)
{
    size_t n = strlen(needle);
    for (; s + n <= end; s++)
        if (*s == *needle && !memcmp(s, needle, n))
            return s;
    return NULL;
}

/* First needle in [s, end) outside <!-- --> comments. */
static const char *find_live(const char *s, const char *end, const char *needle)
{
    for (;;) {
        const char *q = find(s, end, needle), *c = find(s, end, "<!--"), *ce;
        if (!q || !c || q < c)
            return q;
        if (!(ce = find(c + 4, end, "-->")))
            return NULL;
        s = ce + 3;
    }
}

static int count_live(const char *s, const char *end, const char *needle)
{
    int n = 0;
    while ((s = find_live(s, end, needle))) {
        n++;
        s++;
    }
    return n;
}

static int splice(text *t, size_t at, size_t del, const char *ins)
{
    size_t n = strlen(ins);
    char *p = malloc(t->len - del + n + 1);
    if (!p)
        return 0;
    memcpy(p, t->p, at);
    memcpy(p + at, ins, n);
    memcpy(p + at + n, t->p + at + del, t->len - at - del);
    t->len = t->len - del + n;
    p[t->len] = 0;
    free(t->p);
    t->p = p;
    return 1;
}

/* The quest's text [*b, *e): exactly one live <Quest Name="name">. */
static int quest_block(const text *t, const char *name, size_t *b, size_t *e)
{
    char needle[160];
    const char *end = t->p + t->len, *q, *qe;
    snprintf(needle, sizeof(needle), "<Quest Name=\"%s\"", name);
    if (!(q = find_live(t->p, end, needle)) || find_live(q + 1, end, needle) ||
        !(qe = find(q, end, "</Quest>")))
        return 0;
    *b = (size_t)(q - t->p);
    *e = (size_t)(qe - t->p);
    return 1;
}

/* End of the element starting at s: after its "/>" or matching close. */
static const char *element_end(const char *s, const char *end)
{
    char tag[48], open[52], close[52];
    const char *p = s + 1, *gt;
    size_t n = 0;
    int depth = 1;

    while (p < end && n < sizeof(tag) - 1 && *p != ' ' && *p != '>' && *p != '/' && *p != '\n')
        tag[n++] = *p++;
    tag[n] = 0;
    if (!(gt = find(s, end, ">")))
        return NULL;
    if (gt[-1] == '/')
        return gt + 1;
    snprintf(open, sizeof(open), "<%s", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    for (p = gt + 1; p < end;) {
        const char *o = find(p, end, open), *c = find(p, end, close);
        if (!c)
            return NULL;
        if (o && o < c && (o[n + 1] == ' ' || o[n + 1] == '>' || o[n + 1] == '/')) {
            const char *ogt = find(o, end, ">");
            if (!ogt)
                return NULL;
            if (ogt[-1] != '/')
                depth++;
            p = ogt + 1;
            continue;
        }
        if (!--depth)
            return c + strlen(close);
        p = c + strlen(close);
    }
    return NULL;
}

/* --- edits ------------------------------------------------------------- */

enum { E_DONE, E_ALREADY, E_FAILED };

static int edit_off(text *t, const q_edit *ed)
{
    size_t b, e;
    const char *end, *c;
    if (!quest_block(t, ed->quest, &b, &e))
        return E_FAILED;
    end = t->p + e;
    if (find(t->p + b, end, NEVER))
        return E_ALREADY;
    if ((c = find_live(t->p + b, end, "<Conditions>")))
        return splice(t, (size_t)(c - t->p) + strlen("<Conditions>"), 0, "\n      " NEVER) ? E_DONE : E_FAILED;
    if ((c = find_live(t->p + b, end, "<Actions")))
        return splice(t, (size_t)(c - t->p), 0, "<Conditions>" NEVER "</Conditions>\n    ") ? E_DONE : E_FAILED;
    return E_FAILED;
}

static int edit_cut(text *t, const q_edit *ed)
{
    size_t b, e;
    int i, n;
    if (!quest_block(t, ed->quest, &b, &e))
        return E_FAILED;
    n = count_live(t->p + b, t->p + e, ed->start);
    if (n == 0)
        return E_ALREADY;
    if (n != ed->count)
        return E_FAILED;
    for (i = 0; i < n; i++) {
        const char *s, *se;
        size_t from, to;
        if (!quest_block(t, ed->quest, &b, &e) || !(s = find_live(t->p + b, t->p + e, ed->start)) ||
            !(se = element_end(s, t->p + e)))
            return E_FAILED;
        from = (size_t)(s - t->p);
        to = (size_t)(se - t->p);
        /* The whole line when the element is alone on it. */
        while (from > b && (t->p[from - 1] == ' ' || t->p[from - 1] == '\t'))
            from--;
        if (from > b && t->p[from - 1] == '\n') {
            if (t->p[to] == '\r')
                to++;
            if (t->p[to] == '\n')
                to++;
        }
        if (!splice(t, from, to - from, ""))
            return E_FAILED;
    }
    return E_DONE;
}

/* Applies every edit for rel; returns how many changed the text. */
static int apply_edits(const char *rel, text *t, int log)
{
    int counts[3] = { 0, 0, 0 };
    size_t i;
    for (i = 0; i < N_EDITS; i++) {
        int r;
        if (strcmp(s_edits[i].file, rel))
            continue;
        r = s_edits[i].op == Q_OFF ? edit_off(t, &s_edits[i]) : edit_cut(t, &s_edits[i]);
        counts[r]++;
        if (r == E_FAILED && log)
            LOGW("quests: %s: %s %s%s%s not found as expected; skipped", rel, s_edits[i].quest,
                 s_edits[i].op == Q_OFF ? "(off)" : "(cut ", s_edits[i].op == Q_OFF ? "" : s_edits[i].start,
                 s_edits[i].op == Q_OFF ? "" : ")");
    }
    if (log)
        LOGI("quests: %s: %d edits applied, %d already there, %d skipped", rel, counts[E_DONE], counts[E_ALREADY],
             counts[E_FAILED]);
    return counts[E_DONE];
}

/* --- file layer -------------------------------------------------------- */

/* "assets/..." for any spelling of a quest file the game may open
 * (ram://, rom://, backslashes); *hash says it asked for the .hash. */
static const char *quest_file(const char *name, int *hash)
{
    char norm[300], *p = norm;
    size_t i, n;

    snprintf(norm, sizeof(norm), "%s", name);
    for (i = 0; norm[i]; i++)
        if (norm[i] == '\\')
            norm[i] = '/';
    if (!strncmp(p, "ram:", 4) || !strncmp(p, "rom:", 4))
        p += 4;
    while (*p == '/')
        p++;
    n = strlen(p);
    *hash = n > 5 && !strcmp(p + n - 5, ".hash");
    if (*hash)
        p[n - 5] = 0;
    for (i = 0; i < N_EDITS; i++)
        if (!strcmp(p, s_edits[i].file))
            return s_edits[i].file;
    return NULL;
}

int sf_quests_wants(const char *name)
{
    int hash;
    return sf_option_remove_online() && quest_file(name, &hash) != NULL;
}

static void hash_of(const char *rel, const char *data, size_t len, char out[33])
{
    md5_ctx c;
    md5_init(&c);
    md5_update(&c, rel, strlen(rel));
    md5_update(&c, data, len);
    md5_update(&c, sf_plat_device_id(), strlen(sf_plat_device_id()));
    md5_hex(&c, out);
}

/* What the game should read instead of name (a quest file or its .hash):
 * malloc'd, or NULL to read the file as it is. */
void *sf_quests_load(const char *name, uint32_t *size)
{
    static uint32_t logged;   /* bit per s_edits file index */
    char path[320], want[33], got[33];
    const char *rel;
    uint32_t raw_len = 0, hash_len = 0;
    char *raw, *orig_hash, *out = NULL;
    text t;
    int hash, bit, log;
    size_t i;

    if (!(rel = quest_file(name, &hash)))
        return NULL;
    for (i = 0; i < N_EDITS && s_edits[i].file != rel; i++)
        ;
    bit = 1 << (i % 32);
    log = !(__atomic_fetch_or(&logged, bit, __ATOMIC_RELAXED) & bit);

    /* The file and its hash, spelled as the game asked (ram:// or not
     * picks between the cached copy and files.dz's). */
    snprintf(path, sizeof(path), "%s", name);
    if (hash)
        path[strlen(path) - 5] = 0;
    if (!(raw = sf_file_load_raw(path, &raw_len)))
        return NULL;
    strncat(path, ".hash", sizeof(path) - strlen(path) - 1);
    orig_hash = sf_file_load_raw(path, &hash_len);
    if (orig_hash) {
        hash_of(rel, raw, raw_len, want);
        if (hash_len < 32 || memcmp(orig_hash, want, 32)) {
            if (log)
                LOGW("quests: %s.hash is not MD5(path + contents + device ID); file left alone", rel);
            free(raw);
            free(orig_hash);
            return NULL;
        }
    }
    t.p = malloc((size_t)raw_len + 1);
    if (!t.p) {
        free(raw);
        free(orig_hash);
        return NULL;
    }
    memcpy(t.p, raw, raw_len);
    t.p[raw_len] = 0;
    t.len = raw_len;
    free(raw);
    if (!apply_edits(rel, &t, log)) {
        /* Nothing changed: already edited, or nothing to edit. */
        free(t.p);
        free(orig_hash);
        return NULL;
    }
    if (!hash) {
        *size = (uint32_t)t.len;
        free(orig_hash);
        return t.p;
    }
    if (!orig_hash) {
        free(t.p);
        return NULL;
    }
    hash_of(rel, t.p, t.len, got);
    free(t.p);
    free(orig_hash);
    if ((out = malloc(66))) {
        snprintf(out, 66, "%s\n%s", got, got);
        *size = 65;
    }
    return out;
}

/* Harness check (SF_QUESTS_TEST=<dir>): write every edited quest file to
 * dir so it can be compared with the original. */
void sf_quests_selftest(const char *dir)
{
    size_t i, j;
    for (i = 0; i < N_EDITS; i++) {
        char path[400], *p;
        uint32_t len = 0;
        void *data;
        FILE *f;
        for (j = 0; j < i && strcmp(s_edits[j].file, s_edits[i].file); j++)
            ;
        if (j < i)
            continue;
        data = sf_quests_load(s_edits[i].file, &len);
        snprintf(path, sizeof(path), "%s/%s", dir, s_edits[i].file);
        for (p = path + strlen(dir) + 1; *p; p++)
            if (*p == '/')
                *p = '_';
        if (data && (f = fopen(path, "wb"))) {
            fwrite(data, 1, len, f);
            fclose(f);
        }
        LOGI("quests selftest: %s -> %s", s_edits[i].file, data ? path : "(unchanged)");
        free(data);
    }
}
