    // (Included inside CompTests.cpp's anonymous namespace.)
    //==========================================================================
    /** Harmonic distortion of a sine (dB below the fundamental), from the
        last whole number of cycles in [start, end) of channel 0. */
    double distortionDb (const juce::AudioBuffer<float>& b, double rate, double freq, int start, int end)
    {
        const int period = (int) std::lround (rate / freq);
        const int cycles = (end - start) / period;
        const int from = end - cycles * period;
        const auto amplitudeAt = [&] (int harmonic)
        {
            double re = 0.0, im = 0.0;
            for (int i = from; i < end; ++i)
            {
                const double phase = juce::MathConstants<double>::twoPi * freq * harmonic * (double) (i - from) / rate;
                re += (double) b.getSample (0, i) * std::cos (phase);
                im += (double) b.getSample (0, i) * std::sin (phase);
            }
            return std::sqrt (re * re + im * im);
        };

        double harmonics = 0.0;
        for (int h = 2; h <= 10; ++h)
            harmonics += std::pow (amplitudeAt (h), 2.0);

        return 10.0 * std::log10 (harmonics + 1e-30) - 20.0 * std::log10 (amplitudeAt (1) + 1e-30);
    }

    /** Gain reduction (dB) per sample, straight from a Compressor fed one sample at a time. */
    std::vector<float> reductionCurve (Compressor& c, float level, int numSamples)
    {
        juce::AudioBuffer<float> one (2, 1);
        std::vector<float> gr;
        for (int i = 0; i < numSamples; ++i)
        {
            one.setSample (0, 0, level); one.setSample (1, 0, level);
            gr.push_back (c.process (one).maxReductionDb);
        }
        return gr;
    }

    //==========================================================================
    void testVintage()
    {
        std::cout << "\n[CHARACTER - STYLE: VINTAGE]\n";
        constexpr double sr = 48000.0;

        // Its curve: the same as MODERN's once clearly over, slightly different in the knee
        check (near (Compressor::vintageReductionFor (-10.0f, -20.0f, 4.0f), 7.5f, 1e-4f), "10 dB over at 4:1 -> 7.5 dB, the same as MODERN");
        check (near (Compressor::vintageReductionFor (0.0f, -20.0f, 20.0f), 19.0f, 1e-3f), "20 dB over at 20:1 -> 19 dB");
        check (Compressor::vintageReductionFor (-25.0f, -20.0f, 4.0f) == 0.0f, "5 dB under the threshold: untouched");
        check (Compressor::vintageReductionFor (0.0f, -20.0f, 1.0f) == 0.0f, "ratio 1:1: no reduction");
        check (near (Compressor::vintageReductionFor (-20.0f, -20.0f, 4.0f), 0.6f, 0.01f), "at the threshold, 4:1: 0.6 dB (MODERN: 0.56)");
        {
            bool smooth = true;
            float worstApart = 0.0f;
            for (float ratio : { 1.2f, 1.5f, 2.0f, 4.0f, 10.0f, 20.0f })
            {
                float prev = 0.0f;
                for (int i = 0; i <= 8000; ++i)
                {
                    const float level = -60.0f + 0.01f * (float) i;
                    const float gr = Compressor::vintageReductionFor (level, -30.0f, ratio);
                    if (gr + 1e-5f < prev || gr - prev > 0.011f) smooth = false;
                    prev = gr;
                    worstApart = std::max (worstApart, std::abs (gr - Compressor::gainReductionFor (level, -30.0f, ratio)));
                }
            }
            check (smooth, "the curve only rises, with no jumps, at every ratio");
            check (worstApart < 0.6f, "it never strays more than 0.6 dB from MODERN's curve (most: " + juce::String (worstApart, 2) + " dB, at 20:1)");
        }
        {
            Compressor::Settings v; v.vintage = true; v.makeupDb = 3.0f;
            check (near (Compressor::staticOutputDb (-10.0f, v), -10.0f - 7.5f + 3.0f, 1e-3f)
                     && near (Compressor::staticOutputDb (-19.0f, v), -19.0f - Compressor::vintageReductionFor (-19.0f, -20.0f, 4.0f) + 3.0f, 1e-3f),
                   "the graph draws VINTAGE's curve when it's on");
        }

        // A steady tone lands on the curve the graph draws (fast attack, slow release)
        for (float ratio : { 2.0f, 4.0f, 10.0f })
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::character, 1.0f); setParam (p, CompIDs::ratio, ratio);
            setParam (p, CompIDs::attack, 0.1f); setParam (p, CompIDs::release, 1000.0f);
            prepare (p, sr, 512);
            const auto in = sine (sr, 1000.0, fromDb (-8.0f), 1.0);
            const auto out = run (p, in, 512);
            const float outDb = toDb (peak (out, 0, 24000, 48000));
            const float graphDb = Compressor::staticOutputDb (toDb (peak (in, 0, 24000, 48000)), p.getCurrentSettings());
            check (near (outDb, graphDb, 0.3f), "a steady tone at " + juce::String (ratio, 0) + ":1 lands on the graph: "
                                                  + juce::String (outDb, 2) + " vs " + juce::String (graphDb, 2) + " dB");
        }

        // ATTACK means the same as in MODERN while it's grabbing
        Compressor c;
        Compressor::Settings s; s.vintage = true; s.thresholdDb = -30.0f;   // 4:1, 10 ms, 100 ms
        c.prepare (sr, s);
        const float target = Compressor::vintageReductionFor (toDb (0.5f), -30.0f, 4.0f);
        const auto grab = reductionCurve (c, 0.5f, (int) (0.4 * sr));
        const float at10ms = grab[(size_t) (0.010 * sr) - 1];
        check (near (at10ms, target * (1.0f - std::exp (-1.0f)), 0.2f), "10 ms into a 10 ms ATTACK: " + juce::String (at10ms, 2) + " dB (63% of the way: "
                                                                            + juce::String (target * (1.0f - std::exp (-1.0f)), 2) + ")");
        check (near (grab.back(), target, 0.01f), "settles at " + juce::String (grab.back(), 3) + " dB (curve: " + juce::String (target, 3) + ")");

        // While the sound stays loud it follows it closely: 4 dB quieter (still well over the
        // threshold), it covers 63% of the way in RELEASE / sqrt (RATIO) = 50 ms...
        const float quieter = 0.5f * fromDb (-4.0f);
        const float lower = Compressor::vintageReductionFor (toDb (quieter), -30.0f, 4.0f);
        const auto follow = reductionCurve (c, quieter, (int) (0.4 * sr));
        const float at50ms = follow[(size_t) (0.050 * sr) - 1];
        const float expected50 = lower + (target - lower) * std::exp (-1.0f);
        check (near (at50ms, expected50, 0.15f), "while it stays loud it lets go twice as fast at 4:1: after 50 ms " + juce::String (at50ms, 2)
                                                   + " dB (theory " + juce::String (expected50, 2) + "; MODERN: " + juce::String (lower + (target - lower) * std::exp (-0.5f), 2) + ")");

        // ...and when the sound drops away, it eases back in RELEASE x sqrt (RATIO) = 200 ms
        const auto ease = reductionCurve (c, 0.0f, (int) (0.5 * sr));
        const float at200ms = ease[(size_t) (0.200 * sr) - 1];
        check (near (at200ms, follow.back() * std::exp (-1.0f), 0.15f), "...and when the sound drops away it eases back twice as slowly: after 200 ms of quiet "
                                                                          + juce::String (at200ms, 2) + " dB (theory " + juce::String (follow.back() * std::exp (-1.0f), 2)
                                                                          + "; MODERN: " + juce::String (follow.back() * std::exp (-2.0f), 2) + ")");

        // On drums, the meter shows about the same amount as MODERN
        {
            float most[2] {};
            for (int vintage = 0; vintage < 2; ++vintage)
            {
                EasyCompAudioProcessor p;
                setParam (p, CompIDs::character, (float) vintage);
                prepare (p, sr, 480);
                run (p, drumLoop (sr, 8.0), 480, [&] (int start) { if (start == 2 * second) p.takeGainReductionForMeter(); });
                most[vintage] = p.takeGainReductionForMeter();
            }
            check (std::abs (most[1] - most[0]) < 1.5f, "switching STYLE keeps about the same amount on drums: VINTAGE " + juce::String (most[1], 1)
                                                          + " dB, MODERN " + juce::String (most[0], 1) + " dB at the loudest hits");
        }

        // Less distortion on low notes (the volume wobbles less within each wave)
        {
            double thd[2] {};
            for (int vintage = 0; vintage < 2; ++vintage)
            {
                EasyCompAudioProcessor p;
                setParam (p, CompIDs::character, (float) vintage);
                setParam (p, CompIDs::attack, 1.0f); setParam (p, CompIDs::release, 50.0f);
                prepare (p, sr, 512);
                const auto in = sine (sr, 80.0, fromDb (-14.0f), 1.5);   // 6 dB over
                const auto out = run (p, in, 512);
                thd[vintage] = distortionDb (out, sr, 80.0, second / 2, (int) (1.5 * sr));
            }
            check (thd[1] < thd[0] - 1.5, "a low note with fast settings: VINTAGE adds less distortion (" + juce::String (thd[1], 1)
                                            + " dB vs MODERN's " + juce::String (thd[0], 1) + " dB)");
        }

        // Switching while playing: smooth, no clicks
        {
            EasyCompAudioProcessor p;
            prepare (p, sr, 256);
            const auto in = sine (sr, 300.0, fromDb (-3.0f), 3.0);
            const auto out = run (p, in, 256, [&] (int start) { setParam (p, CompIDs::character, (start / 12288) % 2 == 0 ? 0.0f : 1.0f); });
            const float step = worstGainStep (in, out, 4800);
            check (step < 0.005f, "switching MODERN / VINTAGE while playing: no clicks (largest per-sample gain step " + juce::String (step, 5) + ")");
        }

        // Turning RATIO fast while it's compressing: smooth too
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::character, 1.0f); setParam (p, CompIDs::threshold, -40.0f);
            prepare (p, sr, 128);
            const auto in = sine (sr, 300.0, fromDb (-3.0f), 3.0);
            int blockCount = 0;
            const auto out = run (p, in, 128, [&] (int)
            {
                const float phase = (float) (blockCount++ % 150) / 150.0f;
                setParam (p, CompIDs::ratio, 1.0f + 19.0f * (phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f));
            });
            const float step = worstGainStep (in, out, 4800);
            bool finite = true;
            for (int i = 0; i < out.getNumSamples(); ++i)
                finite = finite && std::isfinite (out.getSample (0, i));
            check (finite && step < 0.02f, "sweeping RATIO 1:1 <-> 20:1 every 0.4 s in VINTAGE: no clicks (largest per-sample gain step " + juce::String (step, 5) + ")");
        }

        // AUTO-GAIN still matches
        {
            EasyCompAudioProcessor p;
            loadPresetNamed (p, "Drum Bus - Vintage Glue");
            setParam (p, CommonIDs::autoGain, 1.0f);
            prepare (p, sr, 480);
            const auto loop = drumLoop (sr, 12.0);
            const auto out = run (p, loop, 480);
            const double diff = loudnessDb (out, 6 * second, 12 * second) - loudnessDb (loop, 6 * second, 12 * second);
            check (std::abs (diff) < 0.5, "AUTO-GAIN matches with VINTAGE and THRUST too (" + juce::String (diff, 2) + " dB)");
        }

        // Extremes and sample rates
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::character, 1.0f); setParam (p, CompIDs::lowEnd, 1.0f);
            setParam (p, CompIDs::threshold, -60.0f); setParam (p, CompIDs::ratio, 20.0f); setParam (p, CompIDs::attack, 0.1f);
            setParam (p, CompIDs::release, 10.0f); setParam (p, CompIDs::makeup, 24.0f);
            prepare (p, 44100.0, 64);
            juce::Random rng (42);
            juce::AudioBuffer<float> in (2, 44100);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < in.getNumSamples(); ++i)
                    in.setSample (ch, i, i < 11025 ? (rng.nextFloat() * 2.0f - 1.0f) * 4.0f
                                       : i < 22050 ? 0.0f
                                       : i < 33075 ? ((i % 4410 == 0) ? 1.0f : 1.0e-38f)
                                                   : (rng.nextFloat() * 2.0f - 1.0f) * 1.0e-6f);
            const auto out = run (p, in, 64);
            bool allFinite = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < out.getNumSamples(); ++i)
                    allFinite = allFinite && std::isfinite (out.getSample (ch, i));
            check (allFinite && peak (out, 0, 11025, 22050) == 0.0f, "VINTAGE + THRUST at the most extreme settings: no NaN/inf, silence stays silent");
        }
        {
            float reference = 0.0f;
            juce::String readings;
            bool same = true;
            for (double rate : { 48000.0, 44100.0, 96000.0, 192000.0 })
            {
                EasyCompAudioProcessor p;
                setParam (p, CompIDs::character, 1.0f);
                prepare (p, rate, 512);
                const auto in = sine (rate, 1000.0, fromDb (-6.0f), 1.0);
                const auto out = run (p, in, 512);
                const float outDb = toDb (peak (out, 0, in.getNumSamples() / 2, in.getNumSamples()));
                if (reference == 0.0f) reference = outDb;
                same = same && near (outDb, reference, 0.1f);
                readings << juce::String (outDb, 2) << " ";
            }
            check (same, "VINTAGE sounds the same at 48, 44.1, 96 and 192 kHz (output peaks " + readings.trim() + " dBFS)");
        }
    }

    //==========================================================================
    /** Max gain reduction over the last 0.5 s of a 1.5 s sine (fast attack, slow release), THRUST on or off. */
    float settledWithThrust (double freq, bool thrust, float thresholdDb = -40.0f, float ratio = 20.0f, double rate = 48000.0)
    {
        EasyCompAudioProcessor p;
        setParam (p, CompIDs::threshold, thresholdDb); setParam (p, CompIDs::ratio, ratio);
        setParam (p, CompIDs::attack, 0.1f); setParam (p, CompIDs::release, 1000.0f);
        setParam (p, CompIDs::lowEnd, thrust ? 1.0f : 0.0f);
        prepare (p, rate, 480);
        run (p, sine (rate, freq, fromDb (-6.0f), 1.5), 480, [&] (int start) { if (start >= (int) rate && start < (int) rate + 480) p.takeGainReductionForMeter(); });
        return p.takeGainReductionForMeter();
    }

    void testThrust()
    {
        std::cout << "\n[CHARACTER - LOW END: THRUST]\n";

        // The shape of what the compressor hears
        {
            const auto shape = Compressor::designThrust (48000.0);
            const auto at = [&] (double hz) { return Compressor::thrustResponseDb (shape, hz, 48000.0); };
            check (std::abs (at (1000.0)) < 0.01, "1 kHz is heard unchanged (" + juce::String (at (1000.0), 3) + " dB)");
            check (std::abs (at (100.0) - (-9.8)) < 0.3 && std::abs (at (20.0) - (-16.0)) < 0.5 && std::abs (at (50.0) - (-12.7)) < 0.4,
                   "the low end less and less: 100 Hz " + juce::String (at (100.0), 1) + ", 50 Hz " + juce::String (at (50.0), 1) + ", 20 Hz "
                     + juce::String (at (20.0), 1) + " dB (3 dB per octave)");
            check (std::abs (at (4000.0) - 4.1) < 0.3 && std::abs (at (16000.0) - 4.8) < 0.3,
                   "the bright, edgy range a little more: 4 kHz +" + juce::String (at (4000.0), 1) + ", 16 kHz +" + juce::String (at (16000.0), 1) + " dB");
            bool rising = true;
            for (double hz = 20.0; hz < 20000.0; hz *= 1.05)
                rising = rising && at (hz * 1.05) >= at (hz) - 0.01;
            check (rising, "it only ever rises with frequency (no bumps)");

            bool sameShape = true;
            for (double rate : { 44100.0, 96000.0, 192000.0 })
            {
                const auto other = Compressor::designThrust (rate);
                for (double hz : { 20.0, 60.0, 200.0, 1000.0, 3000.0, 10000.0 })
                    sameShape = sameShape && std::abs (Compressor::thrustResponseDb (other, hz, rate) - at (hz)) < 0.3;
            }
            check (sameShape, "the same shape at 44.1, 96 and 192 kHz");
        }

        // What that does: a 60 Hz 'kick' sets it off much less; 1 kHz the same; 4 kHz a little more
        {
            const auto shape = Compressor::designThrust (48000.0);
            const float kickOff = settledWithThrust (60.0, false), kickOn = settledWithThrust (60.0, true);
            const float expected = 0.95f * (float) -Compressor::thrustResponseDb (shape, 60.0, 48000.0);
            check (near (kickOff - kickOn, expected, 0.4f), "a 60 Hz tone: " + juce::String (kickOff - kickOn, 1) + " dB less reduction with THRUST (theory "
                                                              + juce::String (expected, 1) + " at 20:1)");
            const float midOff = settledWithThrust (1000.0, false), midOn = settledWithThrust (1000.0, true);
            check (near (midOff, midOn, 0.1f), "a 1 kHz tone: the same either way (" + juce::String (midOff, 2) + " vs " + juce::String (midOn, 2) + " dB)");
            const float edgeOff = settledWithThrust (4000.0, false), edgeOn = settledWithThrust (4000.0, true);
            check (near (edgeOn - edgeOff, 0.95f * (float) Compressor::thrustResponseDb (shape, 4000.0, 48000.0), 0.4f),
                   "a 4 kHz tone: " + juce::String (edgeOn - edgeOff, 1) + " dB more reduction with THRUST");

            const float kick44 = settledWithThrust (60.0, true, -40.0f, 20.0f, 44100.0), kick96 = settledWithThrust (60.0, true, -40.0f, 20.0f, 96000.0);
            check (near (kick44, kickOn, 0.3f) && near (kick96, kickOn, 0.3f), "...the same at 44.1 and 96 kHz (" + juce::String (kick44, 1) + ", "
                                                                                + juce::String (kickOn, 1) + ", " + juce::String (kick96, 1) + " dB)");
        }

        // Only what it reacts to: your sound itself is never filtered
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::lowEnd, 1.0f);
            prepare (p, 48000.0, 512);
            const auto in = sine (48000.0, 30.0, fromDb (-30.0f), 1.0);
            const float diff = maxDifference (in, run (p, in, 512));
            check (diff < 1e-6f, "your sound isn't filtered: 30 Hz below the threshold passes untouched with THRUST on (max diff " + juce::String (diff) + ")");
        }

        // With IGNORE BELOW too: both apply
        {
            const float both = [&]
            {
                EasyCompAudioProcessor p;
                setParam (p, CompIDs::threshold, -40.0f); setParam (p, CompIDs::ratio, 20.0f);
                setParam (p, CompIDs::attack, 0.1f); setParam (p, CompIDs::release, 50.0f);   // (so the filters' first wobble is long gone)
                setParam (p, CompIDs::lowEnd, 1.0f); setParam (p, CompIDs::lowCut, 300.0f);
                prepare (p, 48000.0, 480);
                run (p, sine (48000.0, 60.0, fromDb (-6.0f), 1.5), 480, [&] (int start) { if (start == 48000) p.takeGainReductionForMeter(); });
                return p.takeGainReductionForMeter();
            }();
            check (both < 1.0f, "THRUST and IGNORE BELOW 300 Hz together: a 60 Hz tone hardly sets it off (" + juce::String (both, 2) + " dB)");
        }

        // Switching while playing: no clicks
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::threshold, -30.0f);
            prepare (p, 48000.0, 256);
            const auto in = sine (48000.0, 80.0, fromDb (-3.0f), 3.0);
            const auto out = run (p, in, 256, [&] (int start) { setParam (p, CompIDs::lowEnd, (start / 12288) % 2 == 0 ? 0.0f : 1.0f); });
            const float step = worstGainStep (in, out, 4800);
            check (step < 0.005f, "switching THRUST on and off while playing: no clicks (largest per-sample gain step " + juce::String (step, 5) + ")");
        }

        // The graph's dot shows what it reacts to
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::lowEnd, 1.0f);
            prepare (p, 48000.0, 512);
            run (p, sine (48000.0, 100.0, fromDb (-6.0f), 1.0), 512, [&] (int start) { if (start == 24064) p.takeInputLevelForGraph(); });
            const float heard = p.takeInputLevelForGraph();
            const float expected = -6.0f + (float) Compressor::thrustResponseDb (Compressor::designThrust (48000.0), 100.0, 48000.0);
            check (near (heard, expected, 0.3f), "with THRUST the dot shows what it reacts to: 100 Hz at -6 dB reads " + juce::String (heard, 1)
                                                   + " dB (theory " + juce::String (expected, 1) + ")");
        }
    }

    //==========================================================================
    /** Sets the compressor up the way a DAW does, with the sidechain (0 channels = switched off). */
    bool prepareWithSidechain (EasyCompAudioProcessor& p, double rate, int block, int trackChannels = 2, int sidechainChannels = 2)
    {
        juce::AudioProcessor::BusesLayout layout;
        const auto set = [] (int n) { return n == 0 ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::canonicalChannelSet (n); };
        layout.inputBuses.add (set (trackChannels));
        layout.inputBuses.add (set (sidechainChannels));
        layout.outputBuses.add (set (trackChannels));
        const bool ok = p.setBusesLayout (layout);
        p.setRateAndBufferSizeDetails (rate, block);
        p.prepareToPlay (rate, block);
        return ok;
    }

    /** Runs a track (plus what goes into the sidechain, if anything) through, the way a DAW does. */
    juce::AudioBuffer<float> runWithKey (EasyCompAudioProcessor& p, const juce::AudioBuffer<float>& track, const juce::AudioBuffer<float>* key,
                                         int block, std::function<void (int)> beforeBlock = {})
    {
        const int trackChannels = track.getNumChannels();
        const int sidechainChannels = p.getChannelCountOfBus (true, 1);
        const int n = track.getNumSamples();
        juce::AudioBuffer<float> work (trackChannels + sidechainChannels, n);
        work.clear();

        for (int ch = 0; ch < trackChannels; ++ch)
            work.copyFrom (ch, 0, track, ch, 0, n);

        if (key != nullptr && key->getNumChannels() > 0)
            for (int ch = 0; ch < sidechainChannels; ++ch)
                work.copyFrom (trackChannels + ch, 0, *key, std::min (ch, key->getNumChannels() - 1), 0, n);

        juce::MidiBuffer midi;

        for (int start = 0; start < n; start += block)
        {
            if (beforeBlock) beforeBlock (start);
            juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), work.getNumChannels(), start, std::min (block, n - start));
            p.processBlock (view, midi);
        }

        juce::AudioBuffer<float> out (trackChannels, n);
        for (int ch = 0; ch < trackChannels; ++ch)
            out.copyFrom (ch, 0, work, ch, 0, n);
        return out;
    }

    /** Level change (dB) of channel ch between [from, to) seconds, out vs in. */
    float changeDb (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, int ch, double from, double to)
    {
        return toDb (peak (out, ch, (int) (from * 48000.0), (int) (to * 48000.0))) - toDb (peak (in, ch, (int) (from * 48000.0), (int) (to * 48000.0)));
    }

    void testSidechain()
    {
        std::cout << "\n[LISTENS TO: SIDECHAIN]\n";
        constexpr double sr = 48000.0;

        {
            EasyCompAudioProcessor p;
            check (p.getBusCount (true) == 2 && p.getBus (true, 1)->getName() == "Sidechain" && p.getBusCount (false) == 1,
                   "two inputs (your track and the sidechain), one output");

            bool layoutsOk = true;
            for (int track : { 1, 2 })
                for (int side : { 0, 1, 2 })
                {
                    EasyCompAudioProcessor q;
                    layoutsOk = layoutsOk && prepareWithSidechain (q, sr, 512, track, side);
                }
            juce::AudioProcessor::BusesLayout bad;
            bad.inputBuses.add (juce::AudioChannelSet::stereo()); bad.inputBuses.add (juce::AudioChannelSet::stereo());
            bad.outputBuses.add (juce::AudioChannelSet::mono());
            juce::AudioProcessor::BusesLayout surround;
            surround.inputBuses.add (juce::AudioChannelSet::create5point1()); surround.inputBuses.add (juce::AudioChannelSet::stereo());
            surround.outputBuses.add (juce::AudioChannelSet::create5point1());
            check (layoutsOk && ! p.checkBusesLayoutSupported (bad) && ! p.checkBusesLayoutSupported (surround),
                   "mono or stereo tracks, with the sidechain mono, stereo or switched off; in and out must match");
        }

        // A quiet track (under the threshold), turned down only while the sidechain is loud
        const auto track = sine (sr, 220.0, fromDb (-30.0f), 3.0);
        juce::AudioBuffer<float> key (2, 3 * second);
        key.clear();
        {
            const auto burst = sine (sr, 1000.0, fromDb (-6.0f), 1.0);
            for (int ch = 0; ch < 2; ++ch)
                key.copyFrom (ch, second, burst, ch, 0, second);
        }
        const float expected = Compressor::gainReductionFor (-6.0f, -20.0f, 4.0f);

        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::listensTo, 1.0f);
            prepareWithSidechain (p, sr, 512);
            const auto out = runWithKey (p, track, &key, 512);
            const float before = changeDb (out, track, 0, 0.1, 0.95), during = changeDb (out, track, 0, 1.5, 1.95), after = changeDb (out, track, 0, 2.7, 2.99);
            check (std::abs (before) < 0.001f && during < -expected + 1.6f && during > -expected - 0.2f && std::abs (after) < 0.1f,
                   "a quiet track is turned down only while the sidechain is loud (" + juce::String (before, 2) + " / " + juce::String (during, 1) + " / "
                     + juce::String (after, 2) + " dB; the curve says -" + juce::String (expected, 1) + ")");
            check (! p.isSidechainMissing(), "no warning while something's coming into the sidechain");

            bool linked = true;
            for (int i = 0; i < out.getNumSamples(); ++i)
                if (std::abs (track.getSample (1, i)) > 1e-3f)
                    linked = linked && std::abs (out.getSample (0, i) / track.getSample (0, i) - out.getSample (1, i) / track.getSample (1, i)) < 1e-4f;
            check (linked, "both channels of your track get exactly the same gain (none of the sidechain leaks in)");
        }
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::listensTo, 1.0f);
            prepareWithSidechain (p, sr, 512, 2, 1);
            juce::AudioBuffer<float> monoKey (1, 3 * second);
            monoKey.copyFrom (0, 0, key, 0, 0, 3 * second);
            const auto out = runWithKey (p, track, &monoKey, 512);
            const float during = changeDb (out, track, 1, 1.5, 1.95);
            check (during < -expected + 1.6f && during > -expected - 0.2f && std::abs (changeDb (out, track, 1, 0.1, 0.95)) < 0.001f,
                   "a mono sidechain on a stereo track works too");
        }
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::listensTo, 1.0f); setParam (p, CompIDs::character, 1.0f); setParam (p, CompIDs::lowEnd, 1.0f);
            prepareWithSidechain (p, sr, 512);
            const auto out = runWithKey (p, track, &key, 512);
            const float during = changeDb (out, track, 0, 1.5, 1.95);
            const float curve = Compressor::vintageReductionFor (-6.0f, -20.0f, 4.0f);
            check (during < -curve + 1.6f && during > -curve - 0.2f, "with VINTAGE and THRUST, the sidechain drives it the same way (" + juce::String (during, 1)
                                                  + " dB; the curve says -" + juce::String (curve, 1) + ")");
        }

        // A loud track with nothing coming into the sidechain: nothing to react to - and the window says why
        {
            const auto loud = sine (sr, 220.0, fromDb (-6.0f), 3.0);
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::listensTo, 1.0f);
            prepareWithSidechain (p, sr, 512, 2, 0);
            const auto out = runWithKey (p, loud, nullptr, 512);
            check (maxDifference (loud, out) < 1e-6f && p.isSidechainMissing(), "with the sidechain switched off in the DAW it doesn't compress - and the window says why");

            setParam (p, CompIDs::listensTo, 0.0f);
            const auto own = runWithKey (p, loud, nullptr, 512);
            check (! p.isSidechainMissing() && changeDb (own, loud, 0, 2.0, 2.9) < -8.0f,
                   "back on THIS TRACK, it compresses your track again and the warning goes");
        }
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::listensTo, 1.0f);
            prepareWithSidechain (p, sr, 512);
            juce::AudioBuffer<float> silence (2, 3 * second);
            silence.clear();
            runWithKey (p, silence, nullptr, 512);
            check (! p.isSidechainMissing(), "no warning while the track itself is silent (e.g. the DAW is stopped)");
        }

        // The graph's dot follows the sidechain
        {
            EasyCompAudioProcessor p;
            setParam (p, CompIDs::listensTo, 1.0f);
            prepareWithSidechain (p, sr, 512);
            runWithKey (p, track, &key, 512, [&] (int start) { if (start == 96256) p.takeInputLevelForGraph(); });
            check (p.takeInputLevelForGraph() < -100.0f, "the graph's dot follows the sidechain (silent again after the burst)");
            EasyCompAudioProcessor q;
            setParam (q, CompIDs::listensTo, 1.0f);
            prepareWithSidechain (q, sr, 512);
            runWithKey (q, track, &key, 512, [&] (int start) { if (start == 69632) q.takeInputLevelForGraph(); });
            const float level = q.takeInputLevelForGraph();
            check (near (level, -6.0f, 0.2f), "...reading the sidechain's level (" + juce::String (level, 2) + " dB), not your track's");
        }

        // The ducking preset: the kick turns the bass down on every hit
        {
            EasyCompAudioProcessor p;
            loadPresetNamed (p, "Bass - Ducked by the Kick");
            setParam (p, CompIDs::listensTo, 1.0f);
            prepareWithSidechain (p, sr, 480);
            const auto bass = sine (sr, 55.0, fromDb (-12.0f), 4.0);
            const auto kicks = drumLoop (sr, 4.0, -6.0f, -100.0f, -100.0f);   // just the kick, on beats 1 and 3
            const auto out = runWithKey (p, bass, &kicks, 480);
            const float onKick = changeDb (out, bass, 0, 2.0, 2.06), between = changeDb (out, bass, 0, 2.6, 2.95);
            check (onKick < -3.0f && between > -1.0f, "'Bass - Ducked by the Kick': the bass dips " + juce::String (-onKick, 1)
                                                        + " dB on each kick, and is back within " + juce::String (-between, 1) + " dB before the next");
        }
    }
