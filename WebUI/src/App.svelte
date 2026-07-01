<script lang="ts">
    import {
        params, setParameterValue,
        smoothSelected, densitySelected,
        toNormalized, sizeToNormalized
    } from './state/store';
    import { bridge } from './bridge/bridge';
    import type { ParameterId } from './types/parameters';

    import NeuKnob          from './components/NeuKnob.svelte';
    import NeuButton        from './components/NeuButton.svelte';
    import NeuSelector      from './components/NeuSelector.svelte';
    import NeuNumber        from './components/NeuNumber.svelte';
    import FilterGraph      from './components/FilterGraph.svelte';
    import NeuDiffusionNetworkGraph from './components/NeuDiffusionNetworkGraph.svelte';
    import NeuKnobDiscrete  from './components/NeuKnobDiscrete.svelte';
    import Tooltip          from './components/Tooltip.svelte';
    import { formatTime }   from './utils/format';

    const {
        erEnabled, erAmount, erRate, erShape,
        crossoverFreq, diffusion, scale, decay, damping, feedback,
        highFilterType,
        chorusEnabled, chorusAmount, chorusRate,
        reflectGain, diffuseGain, dryWet,
        predelay, smooth, size, freeze,
        flatEnabled, cutEnabled, stereo, density,
    } = params;

    // ── Local UI state ────────────────────────────────────────
    let activeBand: 'low' | 'high' = 'low';
    let bypassed = false;  // 2a: master bypass

    // ── Bridge helpers ────────────────────────────────────────
    // 5d: NaN guard — every send() call is protected
    function send(id: ParameterId, value: number) {
        if (!isFinite(value) || isNaN(value)) return;
        setParameterValue(id, value);
        bridge.sendParameterChange(id, value);
    }
    function sendBool(id: ParameterId, active: boolean) {
        send(id, active ? 1 : 0);
    }
    function sendSelector(id: ParameterId, selected: number, numOptions: number) {
        send(id, selected / (numOptions - 1));
    }

    // ── Diffusion Network — denormalize crossoverFreq for NeuNumber ──
    $: crossoverDisp = +(200 + 7800 * Math.pow($crossoverFreq, 2)).toFixed(0);
</script>

<main class="plugin-root">

    <!-- ========== TOP BAR ========== -->
    <div class="top-bar">
        <div class="plugin-title">
            <span class="title-main">Reverbo</span>
        </div>
        <!-- 2a: Master bypass -->
        <div class="top-bar-right">
            <NeuButton
                label="BYPASS"
                icon="⏻"
                active={bypassed}
                pill
                glow
                on:change={e => {
                    bypassed = e.detail.active;
                    bridge.sendParameterChange('bypass' as ParameterId, bypassed ? 1 : 0);
                }}
            />
        </div>
    </div>

    <!-- 2a: Bypassed overlay banner -->
    {#if bypassed}
        <div class="bypass-banner">BYPASSED</div>
    {/if}

    <!-- ========== MAIN PANELS ========== -->
    <!-- 2a: dim entire panel area when bypassed -->
    <div class="panels-row" class:bypassed>

        <!-- ===== PANEL 1: INPUT ===== -->
        <section class="panel panel-input">
            <div class="panel-title">Input</div>
            <div class="panel-body">

                <FilterGraph />

                <div class="subsection-label">Predelay</div>
                <NeuKnob label="Predelay" value={$predelay} min={0} max={500} unit=" ms"
                    on:change={e => send('predelay', e.detail.value)} />

            </div>
        </section>

        <!-- ===== PANEL 2: EARLY REFLECTIONS ===== -->
        <section class="panel panel-early-ref">
            <div class="panel-title">Early Reflections</div>
            <div class="panel-body">

                <NeuButton
                    label="Spin"
                    active={$erEnabled > 0.5}
                    icon="↺"
                    on:change={e => sendBool('erEnabled', e.detail.active)}
                />

                <div class="row">
                    <NeuNumber label="Amount"
                        value={+(2 + 53 * Math.pow($erAmount, 2)).toFixed(1)}
                        min={2} max={55} step={0.5} decimals={1}
                        on:change={e => send('erAmount', Math.pow((e.detail.value - 2) / 53, 0.5))}
                    />
                    <NeuNumber label="Rate"
                        value={+(0.07 + 1.23 * Math.pow($erRate, 2)).toFixed(2)}
                        min={0.07} max={1.3} step={0.01} unit=" Hz" decimals={2}
                        on:change={e => send('erRate', Math.pow((e.detail.value - 0.07) / 1.23, 0.5))}
                    />
                </div>

                <div class="subsection-label">Shape</div>
                <div class="row">
                    <NeuKnob label="Shape" value={$erShape} min={0} max={1} unit=""
                        on:change={e => send('erShape', e.detail.value)} />
                    <NeuKnob label="Reflect" value={$reflectGain} min={-30} max={6} unit=" dB"
                        on:change={e => send('reflectGain', e.detail.value)} />
                </div>

            </div>
        </section>

        <!-- ===== PANEL 3: DIFFUSION NETWORK ===== -->
        <section class="panel panel-diffusion">
            <div class="panel-title">Diffusion Network</div>
            <div class="panel-body">

                <!-- Row 1: Band selectors + filter type toggle -->
                <div class="dng-header">
                    <div class="band-btns">
                        <NeuButton label="Low"
                            active={activeBand === 'low'}
                            on:change={() => activeBand = 'low'}
                        />
                        <NeuButton label="High"
                            active={activeBand === 'high'}
                            on:change={() => activeBand = 'high'}
                        />
                    </div>
                    <NeuSelector
                        options={['LP', 'Shelf']}
                        selected={$highFilterType > 0.5 ? 1 : 0}
                        on:change={e => send('highFilterType', e.detail.selected)}
                    />
                </div>

                <!-- Row 2: Parameter readouts -->
                <div class="dng-params">
                    <!-- Low band -->
                    <div class="dng-band">
                        <span class="node-dot dot-teal"></span>
                        <NeuNumber label="CROSS"
                            value={+crossoverDisp}
                            min={200} max={8000} step={10} unit=" Hz" decimals={0}
                            on:change={e => send('crossoverFreq', toNormalized(e.detail.value, 200, 8000, 0.5))}
                        />
                        <NeuNumber label="DIFF"
                            value={+$diffusion.toFixed(2)}
                            min={0} max={1} step={0.01} decimals={2}
                            on:change={e => send('diffusion', e.detail.value)}
                        />
                    </div>

                    <div class="dng-divider"></div>

                    <!-- High band -->
                    <div class="dng-band">
                        <span class="node-dot dot-orange" class:dimmed={$highFilterType > 0.5}></span>
                        <NeuNumber label="DAMP"
                            value={+$damping.toFixed(2)}
                            min={0} max={1} step={0.01} decimals={2}
                            on:change={e => send('damping', e.detail.value)}
                        />
                        <div class="feed-wrap" class:disabled={$highFilterType > 0.5}>
                            <NeuNumber label="FEED"
                                value={+$feedback.toFixed(2)}
                                min={0} max={1} step={0.01} decimals={2}
                                on:change={$highFilterType > 0.5 ? undefined : e => send('feedback', e.detail.value)}
                            />
                        </div>
                    </div>
                </div>

                <!-- Row 3: Graph (full width) -->
                <NeuDiffusionNetworkGraph {activeBand} />

                <!-- Diffusion + Scale readouts -->
                <div class="row" style="gap:10px;">
                    <NeuNumber label="Diffusion"
                        value={+($diffusion * 100).toFixed(1)}
                        min={0} max={100} step={1} unit="%" decimals={1}
                        on:change={e => send('diffusion', e.detail.value / 100)}
                    />
                    <NeuNumber label="Scale"
                        value={+($scale * 100).toFixed(1)}
                        min={0} max={100} step={1} unit="%" decimals={1}
                        on:change={e => send('scale', e.detail.value / 100)}
                    />
                    <NeuNumber label="Diffuse"
                        value={+(-30 + 36 * $diffuseGain).toFixed(1)}
                        min={-30} max={6} step={0.5} unit=" dB" decimals={1}
                        on:change={e => send('diffuseGain', (e.detail.value + 30) / 36)}
                    />
                </div>

                <!-- Chorus + Size subsections side by side, headers aligned -->
                <div class="subsection-row">

                    <div class="subsection-col">
                        <div class="subsection-label">Chorus</div>
                        <div class="row" style="gap:8px;">
                            <!-- 4d: LED dot gives clear on/off state -->
                            <NeuButton label="On"
                                active={$chorusEnabled > 0.5}
                                icon=":)"
                                led
                                on:change={e => sendBool('chorusEnabled', e.detail.active)}
                            />
                            <div class="col">
                                <NeuNumber label="Amount"
                                    value={+(0.01 + 3.99 * $chorusAmount).toFixed(2)}
                                    min={0.01} max={4.0} step={0.01} decimals={2}
                                    on:change={e => send('chorusAmount', (e.detail.value - 0.01) / 3.99)}
                                />
                                <NeuNumber label="Rate"
                                    value={+(0.01 + 7.99 * Math.pow($chorusRate, 2)).toFixed(2)}
                                    min={0.01} max={8.0} step={0.01} unit=" Hz" decimals={2}
                                    on:change={e => send('chorusRate', Math.pow((e.detail.value - 0.01) / 7.99, 0.5))}
                                />
                            </div>
                        </div>
                    </div>

                    <div class="subsection-col">
                        <div class="subsection-label">Size</div>
                        <div class="col" style="gap:8px;">
                            <NeuKnobDiscrete
                                label="Smooth"
                                options={['Off', 'Low', 'Med', 'High']}
                                selected={$smoothSelected}
                                on:change={e => sendSelector('smooth', e.detail.selected, 4)}
                            />
                            <NeuKnob label="Size" value={+(0.22 * Math.pow(500.0 / 0.22, $size)).toFixed(2)} min={0.22} max={500} unit=""
                                on:change={e => send('size', sizeToNormalized(e.detail.value))} />
                        </div>
                    </div>

                </div>

            </div>
        </section>

        <!-- ===== PANEL 4: DECAY ===== -->
        <section class="panel panel-decay">
            <div class="panel-title">Decay</div>
            <div class="panel-body">

                <!-- 3b: adaptive units: below 1000 ms → "XXX ms", above → "X.XX s" -->
                <NeuKnob
                    label="Decay"
                    value={$decay}
                    min={200} max={60000}
                    formatFn={v => formatTime(v)}
                    on:change={e => send('decay', e.detail.value)}
                />

                <div class="subsection-label">Mode</div>
                <div class="triangle-group">
                    <!-- 2b: glow + pulse icon when frozen -->
                <NeuButton
                        label="Freeze"
                        active={$freeze > 0.5}
                        icon="❄"
                        glow
                        on:change={e => sendBool('freeze', e.detail.active)}
                    />
                    <div class="triangle-top">
                        <NeuButton label="Flat"
                            active={$flatEnabled > 0.5}
                            on:change={e => sendBool('flatEnabled', e.detail.active)}
                        />
                        <NeuButton label="Cut"
                            active={$cutEnabled > 0.5}
                            on:change={e => sendBool('cutEnabled', e.detail.active)}
                        />
                    </div>
                </div>

            </div>
        </section>

        <!-- ===== PANEL 5: OUTPUT ===== -->
        <section class="panel panel-output">
            <div class="panel-title">Output</div>
            <div class="panel-body">

                <NeuKnobDiscrete
                    label="Density"
                    options={['Sparse', 'Low', 'Mid', 'High']}
                    selected={$densitySelected}
                    on:change={e => sendSelector('density', e.detail.selected, 4)}
                />

                <NeuKnob label="Stereo" value={$stereo} min={0} max={120} unit="°"
                    on:change={e => send('stereo', e.detail.value)} />

                <!-- 3a: Dry/Wet is the primary output control — larger, prominent -->
                <NeuKnob
                    label="Dry/Wet"
                    value={$dryWet}
                    min={0} max={100} unit="%"
                    size="large"
                    prominent
                    showTick
                    defaultValue={0.5}
                    on:change={e => send('dryWet', e.detail.value)}
                />

            </div>
        </section>

    </div>

</main>

<!-- 4b: Global tooltip — mounted once, reads from tooltipStore -->
<Tooltip />

<style>
    /* 5e: Font import moved to app.css / main.ts — not duplicated here */

    :global(body) {
        margin: 0;
        padding: 0;
        background: #ede6da;
        font-family: 'DM Sans', sans-serif;
        overflow: hidden;
        user-select: none;
        cursor: default;
    }

    .plugin-root {
        display: flex;
        flex-direction: column;
        background: #ede6da;
        min-height: 100vh;
    }

    /* ── Top bar ── */
    .top-bar {
        display: flex;
        justify-content: space-between;
        align-items: center;
        padding: 10px 20px 8px;
        border-bottom: 1px solid rgba(150,130,100,0.15);
    }
    .plugin-title { display: flex; align-items: baseline; }
    .title-main {
        font-family: 'DM Serif Display', serif;
        font-size: 1.1rem;
        letter-spacing: 0.12em;
        color: rgba(110, 88, 60, 0.85);
    }
    /* 2a: right side of top bar */
    .top-bar-right { display: flex; align-items: center; }

    /* 2a: bypass banner */
    .bypass-banner {
        position: fixed;
        top: 50%;
        left: 50%;
        transform: translate(-50%, -50%);
        z-index: 500;
        font-family: 'DM Sans', sans-serif;
        font-size: 1.4rem;
        font-weight: 200;
        letter-spacing: 0.5em;
        color: #00c8b4;
        pointer-events: none;
        text-shadow: 0 0 20px rgba(0,200,180,0.4);
    }

    /* ── Panels ── */
    .panels-row {
        display: flex;
        flex: 1;
        padding: 12px 12px 8px;
        align-items: stretch;
        overflow: hidden;
        transition: opacity 0.2s ease;
    }

    /* 2a: dim panels when bypassed */
    .panels-row.bypassed { opacity: 0.5; pointer-events: none; }


    .panel {
        background: #ede6da;
        border-radius: 20px;
        box-shadow:
            10px 10px 24px rgba(155,135,105,0.32),
            -7px -7px 20px rgba(255,252,244,0.92);
        padding: 14px 12px;
        display: flex;
        flex-direction: column;
        gap: 10px;
        margin: 0 4px;
    }

    .panel-input      { flex: 1.4; }
    .panel-early-ref  { flex: 1.1; }
    .panel-diffusion  { flex: 2.8; }
    .panel-decay      { flex: 0.9; min-width: 120px; }
    .panel-output     { flex: 0.7; min-width: 90px; }

    .panel-title {
        font-size: 0.58rem;
        font-weight: 300;
        letter-spacing: 0.22em;
        text-transform: uppercase;
        color: rgba(130, 110, 85, 0.65);
        text-align: center;
        padding-bottom: 6px;
        border-bottom: 1px solid rgba(150,130,100,0.12);
    }

    .panel-body {
        display: flex;
        flex-direction: column;
        align-items: center;
        flex: 1;
        gap: 10px;
    }

    /* ── Shared layout ── */
    .row {
        display: flex;
        align-items: center;
        gap: 12px;
    }
    .col {
        display: flex;
        flex-direction: column;
        align-items: center;
        gap: 6px;
    }

    /* ── Subsection separator label ── */
    .subsection-label {
        font-size: 0.5rem;
        font-weight: 300;
        letter-spacing: 0.2em;
        text-transform: uppercase;
        color: rgba(140, 115, 82, 0.4);
        width: 100%;
        text-align: center;
        padding-top: 4px;
        border-top: 1px solid rgba(150,130,100,0.08);
        margin-top: 2px;
    }

    /* ── Diffusion Network: header row ── */
    .dng-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        width: 100%;
        gap: 8px;
    }
    .band-btns {
        display: flex;
        gap: 8px;
    }

    /* ── Diffusion Network: param readout row ── */
    .dng-params {
        display: flex;
        align-items: center;
        gap: 6px;
        width: 100%;
        justify-content: center;
    }
    .dng-band {
        display: flex;
        align-items: center;
        gap: 8px;
    }
    .dng-divider {
        width: 1px;
        height: 28px;
        background: rgba(150,130,100,0.18);
        flex-shrink: 0;
        margin: 0 4px;
    }
    .node-dot {
        width: 8px;
        height: 8px;
        border-radius: 50%;
        flex-shrink: 0;
    }
    .dot-teal   { background: #00c8b4; box-shadow: 0 0 4px rgba(0,200,180,0.5); }
    .dot-orange { background: #f5a623; box-shadow: 0 0 4px rgba(245,166,35,0.5); }
    .dot-orange.dimmed { background: #b8924a; box-shadow: none; opacity: 0.45; }

    .feed-wrap { display: contents; }
    .feed-wrap.disabled {
        display: block;
        opacity: 0.35;
        pointer-events: none;
    }

    /* ── Chorus + Size side-by-side subsections ── */
    .subsection-row {
        display: flex;
        width: 100%;
        gap: 12px;
        align-items: flex-start;
    }
    .subsection-col {
        flex: 1;
        display: flex;
        flex-direction: column;
        align-items: center;
        gap: 8px;
    }
    /* Subsection label inside a col gets no left-align override */
    .subsection-col .subsection-label {
        width: 100%;
    }

    /* ── Decay panel ── */
    .triangle-group {
        display: flex;
        flex-direction: column;
        align-items: center;
        gap: 6px;
    }
    .triangle-top {
        display: flex;
        gap: 10px;
    }
</style>
