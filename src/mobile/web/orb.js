/**
 * Kity Liquid Siri Orb Renderer (Canvas 2D / 120Hz)
 * Direct visual port of SiriOrbAnimation.qml with fluid audio reactivity.
 */

class SiriOrb {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.state = 'idle'; // 'idle', 'listening', 'thinking', 'speaking', 'error'
    this.audioLevel = 0.0;
    this.targetAudioLevel = 0.0;
    this.rotation = 0;
    this.time = 0;
    this.animId = null;

    // Handle high DPI displays (Retina / OLED 120Hz)
    this.dpr = window.devicePixelRatio || 1;
    this.resize();
    window.addEventListener('resize', () => this.resize());
  }

  resize() {
    const rect = this.canvas.getBoundingClientRect();
    const size = Math.min(rect.width || 320, 340);
    this.canvas.width = size * this.dpr;
    this.canvas.height = size * this.dpr;
    this.ctx.scale(this.dpr, this.dpr);
    this.width = size;
    this.height = size;
  }

  setState(newState) {
    this.state = newState;
  }

  setAudioLevel(level) {
    this.targetAudioLevel = Math.max(0, Math.min(1, level));
  }

  getPrimaryColor() {
    switch (this.state) {
      case 'listening': return '#00e5ff'; // Cyan
      case 'thinking': return '#b388ff';  // Purple
      case 'speaking': return '#00e676';  // Green
      case 'error': return '#ff5252';     // Red
      default: return '#29b6f6';          // Idle light blue
    }
  }

  getSecondaryColor() {
    switch (this.state) {
      case 'listening': return '#ff4081'; // Pink
      case 'thinking': return '#448aff';  // Blue
      case 'speaking': return '#18ffff';  // Bright cyan
      default: return '#7c4dff';          // Idle violet
    }
  }

  start() {
    if (!this.animId) {
      this.loop();
    }
  }

  stop() {
    if (this.animId) {
      cancelAnimationFrame(this.animId);
      this.animId = null;
    }
  }

  loop() {
    this.update();
    this.draw();
    this.animId = requestAnimationFrame(() => this.loop());
  }

  update() {
    this.time += 0.035;
    // Smooth audio level interpolation (low-pass filter)
    this.audioLevel += (this.targetAudioLevel - this.audioLevel) * 0.22;

    if (this.state === 'thinking') {
      this.rotation += 0.045;
    } else if (this.state === 'listening') {
      this.rotation += 0.02 + this.audioLevel * 0.05;
    } else {
      this.rotation += 0.008;
    }
  }

  draw() {
    const ctx = this.ctx;
    const cx = this.width / 2;
    const cy = this.height / 2;
    const baseRadius = (this.width / 2) * 0.52;

    ctx.clearRect(0, 0, this.width, this.height);

    const primary = this.getPrimaryColor();
    const secondary = this.getSecondaryColor();

    ctx.save();
    ctx.translate(cx, cy);
    ctx.rotate(this.rotation);

    // 1. Outer Pulsing Glow Aura
    const pulseFactor = this.state === 'listening' 
      ? 1 + Math.sin(this.time * 4) * 0.08 + this.audioLevel * 0.35
      : 1 + Math.sin(this.time * 1.5) * 0.04;
    const auraRadius = baseRadius * 1.35 * pulseFactor;

    const auraGrad = ctx.createRadialGradient(0, 0, baseRadius * 0.4, 0, 0, auraRadius);
    auraGrad.addColorStop(0, hexToRgba(primary, 0.45));
    auraGrad.addColorStop(0.5, hexToRgba(secondary, 0.25));
    auraGrad.addColorStop(1, 'rgba(0, 0, 0, 0)');

    ctx.fillStyle = auraGrad;
    ctx.beginPath();
    ctx.arc(0, 0, auraRadius, 0, Math.PI * 2);
    ctx.fill();

    // 2. Liquid Wave Rings (3 Harmonically Deformed Waves)
    const waveCount = 3;
    for (let w = 0; w < waveCount; w++) {
      ctx.save();
      const wavePhase = this.time * (1.8 + w * 0.4);
      const waveRotation = (Math.PI * 2 / waveCount) * w + (w % 2 === 0 ? this.rotation : -this.rotation * 0.8);
      ctx.rotate(waveRotation);

      ctx.beginPath();
      const points = 48;
      for (let i = 0; i <= points; i++) {
        const theta = (Math.PI * 2 / points) * i;
        const deform = Math.sin(theta * 3 + wavePhase) * (8 + this.audioLevel * 28);
        const r = baseRadius * 0.95 + deform;
        const px = Math.cos(theta) * r;
        const py = Math.sin(theta) * r;

        if (i === 0) ctx.moveTo(px, py);
        else ctx.lineTo(px, py);
      }
      ctx.closePath();

      const waveGrad = ctx.createLinearGradient(-baseRadius, -baseRadius, baseRadius, baseRadius);
      waveGrad.addColorStop(0, hexToRgba(primary, 0.55));
      waveGrad.addColorStop(1, hexToRgba(secondary, 0.35));

      ctx.strokeStyle = waveGrad;
      ctx.lineWidth = 2.5 + this.audioLevel * 4;
      ctx.shadowColor = primary;
      ctx.shadowBlur = 12 + this.audioLevel * 15;
      ctx.stroke();
      ctx.restore();
    }

    // 3. Inner Liquid Core Sphere
    ctx.beginPath();
    const coreRadius = baseRadius * (0.86 + this.audioLevel * 0.15);
    ctx.arc(0, 0, coreRadius, 0, Math.PI * 2);

    const coreGrad = ctx.createRadialGradient(-coreRadius * 0.3, -coreRadius * 0.3, 0, 0, 0, coreRadius);
    coreGrad.addColorStop(0, hexToRgba('#ffffff', 0.85));
    coreGrad.addColorStop(0.25, hexToRgba(primary, 0.75));
    coreGrad.addColorStop(0.7, hexToRgba(secondary, 0.8));
    coreGrad.addColorStop(1, hexToRgba('#06070a', 0.95));

    ctx.fillStyle = coreGrad;
    ctx.shadowColor = primary;
    ctx.shadowBlur = 24;
    ctx.fill();

    // 4. Specular Glass Highlight (Liquid Glass Reflection)
    ctx.beginPath();
    ctx.ellipse(0, -coreRadius * 0.45, coreRadius * 0.55, coreRadius * 0.22, 0, 0, Math.PI * 2);
    const specGrad = ctx.createLinearGradient(0, -coreRadius * 0.65, 0, -coreRadius * 0.2);
    specGrad.addColorStop(0, 'rgba(255, 255, 255, 0.65)');
    specGrad.addColorStop(1, 'rgba(255, 255, 255, 0.0)');
    ctx.fillStyle = specGrad;
    ctx.shadowBlur = 0;
    ctx.fill();

    ctx.restore();
  }
}

function hexToRgba(hex, alpha) {
  let c = hex.replace('#', '');
  if (c.length === 3) c = c.split('').map(x => x + x).join('');
  const num = parseInt(c, 16);
  const r = (num >> 16) & 255;
  const g = (num >> 8) & 255;
  const b = num & 255;
  return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

window.SiriOrb = SiriOrb;
