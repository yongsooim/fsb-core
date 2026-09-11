// Audio render thread, and the device the engine plays into. It holds the PCM
// and every voice's own sample position, and mixes on the audio clock, so the
// sound follows real time no matter what the game clock is doing: a speed
// change is not something it can hear, and nothing seeks on the way in or out.
//
// The engine sends the same commands the native host's device mixer consumes —
// load, play, stop, volume, loop — and this is a port of that mixer
// (AudioOutput::mix), down to the integer arithmetic, so what a browser plays
// is what the native build plays.
//
// Commands arrive by postMessage rather than a SharedArrayBuffer so the page
// does not need cross-origin isolation to be served.
const RATE = 44100;
const LOAD = 0, PLAY = 1, STOP = 2, VOLUME = 3, COMPLETE = 4, LOOP = 5;

class FsbAudio extends AudioWorkletProcessor {
    constructor() {
        super();
        this.voices = Array.from({ length: 17 }, () => ({ pcm: null, handle: 0, volume: 100, loop: false, playing: false, phase: 0 }));
        this.waves = new Map();  // key -> { samples, rate, channels, frames }
        this.gain = null;        // Q24 amplitude per volume percent, from the engine.
        this.silentFrames = 0;
        this.energy = 0;         // Reported as a level, the only thing a headless
        this.frames = 0;         // run can check about sound that never leaves here.
        this.port.onmessage = event => this.handle(event.data);
    }

    handle(message) {
        if (message.type === 'gain') { this.gain = message.gain; return; }
        if (message.type === 'reset') { for (const voice of this.voices) { voice.pcm = null; voice.playing = false; } return; }
        const voice = this.voices[message.slot];
        if (message.samples) this.waves.set(message.key, { samples: message.samples, rate: message.rate, channels: message.channels, frames: message.frames });
        switch (message.kind) {
        // Load and play carry the whole voice; the rest only touch the voice
        // they were issued for, which is how a stale handle stops being able to
        // stop or requantize a track that has already been replaced.
        case LOAD: case PLAY:
            voice.pcm = this.waves.get(message.key) || null;
            voice.handle = message.handle; voice.volume = message.volume;
            voice.loop = message.loop; voice.playing = message.playing; voice.phase = message.phase;
            break;
        case STOP: if (voice.handle === message.handle) voice.playing = false; break;
        case VOLUME: if (voice.handle === message.handle) voice.volume = message.volume; break;
        case LOOP: if (voice.handle === message.handle) voice.loop = message.loop; break;
        case COMPLETE: break; // The device finishes at its own sample rate.
        }
    }

    process(inputs, outputs) {
        const [left, right] = outputs[0];
        if (!this.gain) return true;
        for (let frame = 0; frame < left.length; ++frame) {
            let sumLeft = 0, sumRight = 0, playing = 0;
            for (const voice of this.voices) {
                if (!voice.playing || !voice.pcm) continue;
                ++playing;
                const pcm = voice.pcm;
                const index = Math.floor(voice.phase / RATE), fraction = voice.phase % RATE;
                const next = index + 1 < pcm.frames ? index + 1 : voice.loop ? 0 : index;
                const gain = this.gain[voice.volume];
                for (let channel = 0; channel < 2; ++channel) {
                    const source = pcm.channels === 1 ? 0 : channel;
                    const a = pcm.samples[index * pcm.channels + source], b = pcm.samples[next * pcm.channels + source];
                    const sample = a + Math.trunc((b - a) * fraction / RATE);
                    const scaled = Math.trunc(sample * gain / 16777216);
                    if (channel === 0) sumLeft += scaled; else sumRight += scaled;
                }
                voice.phase += pcm.rate;
                if (voice.phase >= pcm.frames * RATE) {
                    if (voice.loop) voice.phase %= pcm.frames * RATE; else voice.playing = false;
                }
            }
            // Clamping to int16 before the float conversion keeps a loud mix
            // sounding the way it does on the native output rather than
            // exceeding it and being clipped somewhere further along.
            const sample = Math.max(-32768, Math.min(32767, sumLeft)) / 32768;
            left[frame] = sample;
            right[frame] = Math.max(-32768, Math.min(32767, sumRight)) / 32768;
            this.energy += sample * sample; ++this.frames;
            if (!playing) ++this.silentFrames;
        }
        if (currentFrame % 16384 < left.length) {
            this.port.postMessage({ voices: this.voices.filter(voice => voice.playing).length,
                                    silentFrames: this.silentFrames,
                                    level: Math.sqrt(this.energy / Math.max(1, this.frames)) });
            this.energy = 0; this.frames = 0;
        }
        return true;
    }
}
registerProcessor('fsb-audio', FsbAudio);
