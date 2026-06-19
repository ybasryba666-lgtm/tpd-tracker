/**
 * =============================================================================
 * TPD Tracker — Time Pressure Density real-time overlay for Geometry Dash
 * =============================================================================
 *
 * Displays a running "Time Pressure Density" score on-screen, reflecting how
 * mechanically demanding each click in the current attempt is.
 *
 * Per-click formula:
 *
 *   Click_TPD = (100 / W) × (1 + C / ΔT) × (1 + L)
 *
 *   W   — click-window size in milliseconds  (see estimateWindowSize)
 *   ΔT  — seconds since the last click, clamped to ≥ 0.01 s
 *   C   — game-mode constant (Wave/Swing 2.0 | Ship 1.5 | Ball/UFO 1.2 | else 1.0)
 *   L   — normalised level progress ∈ [0.0, 1.0]
 *
 * Each Click_TPD is added to a running Total_TPD for the current attempt.
 * Both values are shown in a CCLabelBMFont overlay attached to UILayer.
 * =============================================================================
 */

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>    // std::clamp, std::max
#include <cstdio>       // std::snprintf

using namespace geode::prelude;


// ══════════════════════════════════════════════════════════════════════════════
// HUD layout constants
// ══════════════════════════════════════════════════════════════════════════════

/** Uniform scale applied to bigFont.fnt (which is large by default). */
static constexpr float kHudScale   = 0.45f;

/**
 * Pixels from the top-left corner of the screen.
 * kHudOffsetY is intentionally large enough to clear GD's pause button icon,
 * which sits roughly in the top-left quadrant of UILayer.
 */
static constexpr float kHudOffsetX = 8.0f;
static constexpr float kHudOffsetY = 42.0f;

/** Z-order inside UILayer — high enough to render above most built-in nodes. */
static constexpr int   kHudZOrder  = 100;

/** Label opacity: 0–255. 210 gives a slightly transparent, non-obtrusive look. */
static constexpr GLubyte kHudOpacity = 210;


// ══════════════════════════════════════════════════════════════════════════════
// Formula constants
// ══════════════════════════════════════════════════════════════════════════════

/** ΔT used for the very first click of an attempt (no previous timestamp). */
static constexpr float kFirstClickDT = 1.0f;

/** Absolute minimum ΔT to prevent division-by-zero in the density term. */
static constexpr float kMinDeltaT    = 0.01f;

/** Fallback W used if estimateWindowSize() returns a non-positive value. */
static constexpr float kFallbackW    = 16.7f;


// ══════════════════════════════════════════════════════════════════════════════
// estimateWindowSize  —  placeholder for hitbox-overlap window logic
// ══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Returns the "valid click window" W in milliseconds.
 *
 * W represents how long the player could press the action button and still
 * clear the upcoming obstacle cleanly.  A smaller W → tighter timing →
 * higher Click_TPD.
 *
 * CURRENT IMPLEMENTATION
 * ──────────────────────
 * Returns a fixed 16.7 ms baseline (one frame at 60 FPS), which is a
 * conservative approximation for average GD obstacles.
 *
 * HOW TO INJECT REAL HITBOX-OVERLAP LOGIC
 * ────────────────────────────────────────
 * Replace the body below with the following steps:
 *
 *  1) Obtain the player's "gameplay" bounding rect (already mode-adjusted,
 *     smaller than the visual sprite):
 *
 *         CCRect pr = player->getObjectRect();
 *
 *  2) Gather nearby hazard objects.
 *     Iterate PlayLayer::m_gameLayer's children, filtering to objects whose
 *     x-position is within the next ~300 units ahead of the player.
 *     For performance, maintain a spatial-hash / sorted list updated in
 *     PlayLayer::postUpdate.
 *
 *  3) Swept AABB — horizontal axis:
 *
 *         float vx = player->m_playerSpeed;       // units per second
 *         // For a given hazardRect hr:
 *         float enterX = (hr.getMinX() - pr.getMaxX()) / vx;
 *         float exitX  = (hr.getMaxX() - pr.getMinX()) / vx;
 *
 *  4) Swept AABB — vertical axis:
 *
 *         float vy = player->m_yVelocity;         // units per second
 *         float enterY = (hr.getMinY() - pr.getMaxY()) / vy;
 *         float exitY  = (hr.getMaxY() - pr.getMinY()) / vy;
 *
 *     Combined overlap interval:
 *         float t_enter = std::max(enterX, enterY);
 *         float t_exit  = std::min(exitX,  exitY);
 *         if (t_exit > t_enter)
 *             float W = (t_exit - t_enter) * 1000.0f;  // seconds → ms
 *
 *  5) Mode-specific corrections:
 *
 *     Wave (m_isDart):
 *         The hitbox is a thin diagonal sliver — timing windows are
 *         much shorter in practice.  Multiply W by ~0.4f.
 *
 *     Robot (m_isRobot):
 *         Hitbox height changes per animation frame.  Read the rect
 *         from the currently active frame instead of getObjectRect().
 *
 *     Swing (m_isSwing):
 *         Gravity direction flips on each press; factor that into the
 *         swept-AABB y-direction calculation.
 *
 *  6) Take the minimum W across all nearby hazards — the tightest
 *     upcoming obstacle defines the pressure felt by the player.
 *
 * @param player  m_player1 from the PlayLayer instance.  Unused in baseline.
 * @return        Estimated window size in milliseconds (currently 16.7f).
 */
static float estimateWindowSize([[maybe_unused]] PlayerObject* player)
{
    // ── Baseline ─────────────────────────────────────────────────────────────
    // 1000 ms / 60 fps ≈ 16.7 ms per frame.
    // Replace this with the swept-AABB result (Step 4 above) for real values.
    return 16.7f;
}


// ══════════════════════════════════════════════════════════════════════════════
// PlayLayer hook
// ══════════════════════════════════════════════════════════════════════════════

class $modify(TPDTrackerLayer, PlayLayer)
{
    // ── Per-instance state ────────────────────────────────────────────────────
    //
    // Geode allocates one Fields instance per PlayLayer object.
    // Access from any method via: m_fields->fieldName
    //
    struct Fields
    {
        /** Sum of all Click_TPD values recorded in the current attempt. */
        float totalTPD { 0.0f };

        /**
         * PlayLayer::m_time (in seconds) captured at the previous click.
         * A negative sentinel (-1.0) means no click has been made yet in
         * this attempt, so the first ΔT defaults to kFirstClickDT.
         */
        float lastClickTime { -1.0f };

        /**
         * Pointer to the HUD label child of m_uiLayer.
         * Null if label creation failed in init().
         * Lifetime is managed by m_uiLayer (which retains all its children);
         * this is a non-owning raw pointer, valid for the lifetime of PlayLayer.
         */
        CCLabelBMFont* label { nullptr };
    };


    // ── Helper: game-mode constant C ─────────────────────────────────────────

    /**
     * @brief Returns the C coefficient for the current game mode.
     *
     * Higher C = game mode requires more sustained / precise input = more TPD
     * pressure per rapid click.
     *
     * Priority order matters: check specialised modes before falling through
     * to the cube-family default.
     *
     * NOTE: If your Geode codegen uses a different field name for the
     * Swingcopter (e.g. m_isSwingCopter), update the m_isSwing line below.
     */
    float gameModeC()
    {
        PlayerObject* p = m_player1;
        if (!p) return 1.0f;

        // ── 2.0: Wave & Swingcopter ───────────────────────────────────────
        // Both require continuous hold-precision with near-zero error margin.
        if (p->m_isDart)  return 2.0f;   // Wave (internally "dart")
        if (p->m_isSwing) return 2.0f;   // Swingcopter (GD 2.2+)

        // ── 1.5: Ship ─────────────────────────────────────────────────────
        // Sustained directional flight; clicks control altitude continuously.
        if (p->m_isShip)  return 1.5f;

        // ── 1.2: Ball / UFO ───────────────────────────────────────────────
        // Gravity-flip and hold mechanics add moderate timing complexity.
        if (p->m_isBall)  return 1.2f;
        if (p->m_isBird)  return 1.2f;   // UFO (internally "bird")

        // ── 1.0: Cube / Robot / Spider ────────────────────────────────────
        // Single discrete taps; baseline difficulty.
        // m_isRobot and m_isSpider are implicitly covered here.
        return 1.0f;
    }


    // ── Helper: level progress L ─────────────────────────────────────────────

    /**
     * @brief Returns the normalised level progress L ∈ [0.0, 1.0].
     *
     * Uses the ratio of the player's current x-position to m_levelLength.
     * Both are expressed in the same GD world-coordinate space, so no
     * unit conversion is required.
     *
     * Accuracy note:
     *   The player spawns at a small positive x offset (~105 units), so the
     *   very first click returns L slightly above 0.0.  This is negligible
     *   and acceptable for TPD scoring purposes.
     *
     *   If you observe progress saturating at 1.0 earlier than expected,
     *   m_levelLength on your GD build may be in block units (1 block = 30
     *   world units).  In that case multiply m_levelLength by 30.0f below.
     */
    float progressL()
    {
        PlayerObject* p = m_player1;
        if (!p || m_levelLength <= 0.0f) return 0.0f;

        return std::clamp(p->getPositionX() / m_levelLength, 0.0f, 1.0f);
    }


    // ── Helper: update HUD label text ────────────────────────────────────────

    /**
     * @brief Pushes a formatted string to the on-screen CCLabelBMFont.
     *
     * Format:  "TPD: <total> | Last: <clickTPD>"
     *
     * @param clickTPD  The Click_TPD score just calculated.
     */
    void refreshHUD(float clickTPD)
    {
        if (!m_fields->label) return;

        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "TPD: %.1f | Last: %.2f",
            m_fields->totalTPD,
            clickTPD);

        m_fields->label->setString(buf);
    }


    // ── Helper: reset attempt state ──────────────────────────────────────────

    /**
     * @brief Zeros all mutable TPD counters and clears the label text.
     *
     * Does NOT destroy or remove the CCLabelBMFont node; it is reused
     * across deaths and restarts for the full lifetime of the PlayLayer.
     */
    void resetTPDState()
    {
        m_fields->totalTPD      = 0.0f;
        m_fields->lastClickTime = -1.0f;

        if (m_fields->label)
            m_fields->label->setString("TPD: 0.0 | Last: 0.00");
    }


    // ══════════════════════════════════════════════════════════════════════════
    // Hook: PlayLayer::init
    // ══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Creates the HUD overlay label and attaches it to UILayer.
     *
     * Called once per level session (NOT on every death — see resetLevel).
     * We call the original init first so that m_uiLayer, m_player1,
     * m_levelLength, etc. are fully populated before we read them.
     *
     * The label is added to m_uiLayer (the non-scrolling overlay layer),
     * ensuring it remains fixed in the top-left corner of the screen
     * regardless of how far the game world has scrolled.
     *
     * @param level              The GJGameLevel being played.
     * @param useReplay          Whether a replay is active.
     * @param dontCreateObjects  Skip object construction (editor preview).
     * @return false propagates PlayLayer::init failure without crashing.
     */
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects)
    {
        // ── Original init ─────────────────────────────────────────────────
        // MUST be called first: it builds m_uiLayer, game objects, etc.
        if (!PlayLayer::init(level, useReplay, dontCreateObjects))
            return false;

        // ── Guard ─────────────────────────────────────────────────────────
        if (!m_uiLayer)
        {
            log::warn("[TPD Tracker] m_uiLayer is null after PlayLayer::init — HUD skipped.");
            return true;    // level is still playable without the overlay
        }

        // ── Create CCLabelBMFont ──────────────────────────────────────────
        // "bigFont.fnt" is GD's standard bitmap font, always present in the
        // game's resource bundle (no external resource required by this mod).
        auto* lbl = CCLabelBMFont::create("TPD: 0.0 | Last: 0.00", "bigFont.fnt");
        if (!lbl)
        {
            log::warn("[TPD Tracker] CCLabelBMFont::create returned null — HUD skipped.");
            return true;
        }

        // ── Style ─────────────────────────────────────────────────────────
        lbl->setScale(kHudScale);           // shrink the large default glyph size
        lbl->setOpacity(kHudOpacity);       // slight transparency — less distracting
        lbl->setAnchorPoint({ 0.0f, 1.0f }); // top-left corner of the label is the pivot

        // ── Position: top-left corner of the screen ───────────────────────
        // kHudOffsetY is large enough to sit below GD's pause-button icon.
        // Adjust these constants if the label overlaps built-in UI elements
        // on your target platform or screen resolution.
        const CCSize win = CCDirector::sharedDirector()->getWinSize();
        lbl->setPosition({ kHudOffsetX, win.height - kHudOffsetY });

        // ── Add to the non-scrolling UI layer ─────────────────────────────
        // m_uiLayer retains lbl; m_fields->label is a non-owning observer.
        m_uiLayer->addChild(lbl, kHudZOrder);
        m_fields->label = lbl;

        return true;
    }


    // ══════════════════════════════════════════════════════════════════════════
    // Hook: PlayLayer::pushButton
    // ══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Calculates Click_TPD on every primary-button press.
     *
     * Execution order (intentional):
     *   1. Original GD handler fires — game physics / player state updated.
     *   2. Filter: only button 1 (primary action) is tracked.
     *      Button 2/3 control directional movement in platformer mode and
     *      are excluded from density calculations.
     *   3. Compute W, ΔT, C, L.
     *   4. Evaluate Click_TPD and accumulate into totalTPD.
     *   5. Refresh HUD label.
     *
     * @param button   Button index (1 = jump / primary action).
     * @param holding  True when the event represents a sustained hold rather
     *                 than a fresh press (used internally by GD for replays).
     */
    void pushButton(int button, bool holding)
    {
        // ── Let GD handle physics first ───────────────────────────────────
        PlayLayer::pushButton(button, holding);

        // ── Filter: primary action button only ────────────────────────────
        if (button != 1) return;

        // ──────────────────────────────────────────────────────────────────
        // W  — window size (ms)
        // ──────────────────────────────────────────────────────────────────
        float W = estimateWindowSize(m_player1);
        if (W <= 0.0f) W = kFallbackW;     // guard against bad return value

        // ──────────────────────────────────────────────────────────────────
        // ΔT — seconds since the last click
        //
        // PlayLayer::m_time tracks elapsed level time in seconds.
        // It resets to 0 each time resetLevel() is called (on death/restart),
        // which is exactly what we want: fresh ΔT per attempt.
        // ──────────────────────────────────────────────────────────────────
        float now = m_time;
        float dt  = (m_fields->lastClickTime < 0.0f)
                  ? kFirstClickDT                           // no prior click this attempt
                  : (now - m_fields->lastClickTime);

        dt = std::max(dt, kMinDeltaT);                     // prevent ÷0
        m_fields->lastClickTime = now;                     // record for next call

        // ──────────────────────────────────────────────────────────────────
        // C  — game-mode constant
        // ──────────────────────────────────────────────────────────────────
        float C = gameModeC();

        // ──────────────────────────────────────────────────────────────────
        // L  — normalised level progress
        // ──────────────────────────────────────────────────────────────────
        float L = progressL();

        // ──────────────────────────────────────────────────────────────────
        // Click_TPD = (100 / W) × (1 + C / ΔT) × (1 + L)
        //
        // Reading the three factors intuitively:
        //
        //   (100 / W)       Tighter window  → more demanding → scales everything up.
        //   (1 + C / ΔT)    Rapid clicks in a hard mode    → high density bonus.
        //   (1 + L)         Later in the level → higher stakes → proportional reward.
        // ──────────────────────────────────────────────────────────────────
        float clickTPD = (100.0f / W)
                       * (1.0f + (C / dt))
                       * (1.0f + L);

        m_fields->totalTPD += clickTPD;

        // ── Update HUD ────────────────────────────────────────────────────
        refreshHUD(clickTPD);
    }


    // ══════════════════════════════════════════════════════════════════════════
    // Hook: PlayLayer::resetLevel
    // ══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Resets TPD counters when the player dies or manually restarts.
     *
     * PlayLayer::resetLevel() is called on every death and every manual
     * restart (Ctrl+R / restart button).  It does NOT destroy PlayLayer or
     * its children, so the CCLabelBMFont created in init() persists — we
     * simply blank its text and zero our fields.
     *
     * Calling the original first ensures m_time is already reset to 0
     * and the player is repositioned before we clear lastClickTime, so
     * the very next pushButton correctly detects "no previous click."
     */
    void resetLevel()
    {
        PlayLayer::resetLevel();    // GD reset: m_time → 0, player → start pos, …
        resetTPDState();            // TPD reset: totalTPD/lastClickTime → 0/-1, label cleared
    }

}; // class $modify(TPDTrackerLayer, PlayLayer)
