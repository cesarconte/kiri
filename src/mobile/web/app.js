/**
 * Kity Voice Remote - Client Controller (PWA)
 * Manages WebSocket transport, Web Audio RMS analysis, and Web Speech API
 */

(() => {
  // DOM Elements
  const statusDot = document.getElementById('statusDot');
  const screenGlow = document.getElementById('screenGlow');
  const transcriptEl = document.getElementById('transcript');
  const statusSubEl = document.getElementById('statusSub');
  const btnMic = document.getElementById('btnMic');
  const orbCanvas = document.getElementById('orbCanvas');
  const orbWrapper = document.getElementById('orbWrapper');
  const btnSettings = document.getElementById('btnSettings');
  const settingsDialog = document.getElementById('settingsDialog');
  const btnCloseSettings = document.getElementById('btnCloseSettings');
  const btnSaveSettings = document.getElementById('btnSaveSettings');
  const tokenInput = document.getElementById('tokenInput');
  const hapticToggle = document.getElementById('hapticToggle');
  const autoSendToggle = document.getElementById('autoSendToggle');
  const hintText = document.getElementById('hintText');

  // State
  let ws = null;
  let isRecording = false;
  let audioContext = null;
  let analyser = null;
  let micStream = null;
  let recognition = null;
  let reconnectTimer = null;
  let rmsTimer = null;
  let lastFinalTranscript = '';
  let speechSilenceTimer = null;

  // Initialize Siri Orb Canvas
  const orb = new window.SiriOrb(orbCanvas);
  orb.start();

  // Token Management
  function resolveAuthToken() {
    const urlParams = new URLSearchParams(window.location.search);
    const urlToken = urlParams.get('token');
    if (urlToken) {
      localStorage.setItem('kity_mobile_token', urlToken);
      // Clean URL without reloading
      const cleanUrl = window.location.protocol + "//" + window.location.host + window.location.pathname;
      window.history.replaceState({ path: cleanUrl }, '', cleanUrl);
      return urlToken;
    }
    return localStorage.getItem('kity_mobile_token') || '';
  }

  let authToken = resolveAuthToken();

  function triggerHaptic(pattern = 18) {
    if (hapticToggle.checked && navigator.vibrate) {
      try {
        navigator.vibrate(pattern);
      } catch (e) {
        // Ignored
      }
    }
  }

  // WebSocket Connection
  function connectWebSocket() {
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
      return;
    }

    statusDot.className = 'brand-dot connecting';
    statusSubEl.textContent = 'Conectando con Kity...';

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsPort = window.KITY_WS_PORT || (window.location.port ? (parseInt(window.location.port) + 1) : 8766);
    const wsHost = `${window.location.hostname}:${wsPort}`;
    const wsUrl = `${protocol}//${wsHost}/ws?token=${encodeURIComponent(authToken)}`;

    try {
      ws = new WebSocket(wsUrl);
    } catch (e) {
      scheduleReconnect();
      return;
    }

    ws.onopen = () => {
      statusDot.className = 'brand-dot connected';
      statusSubEl.textContent = 'Conectado a CachyOS Plasma 6';
      if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
      }
      // Send handshake
      ws.send(JSON.stringify({ type: 'handshake', client: 'kity-mobile-pwa' }));
    };

    ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data);
        handleServerMessage(msg);
      } catch (err) {
        console.error('Invalid message format:', err);
      }
    };

    ws.onclose = () => {
      statusDot.className = 'brand-dot';
      statusSubEl.textContent = 'Desconectado del PC';
      updateUIState('idle');
      scheduleReconnect();
    };

    ws.onerror = (err) => {
      console.warn('WebSocket error:', err);
      ws.close();
    };
  }

  function scheduleReconnect() {
    if (!reconnectTimer) {
      reconnectTimer = setTimeout(() => {
        reconnectTimer = null;
        connectWebSocket();
      }, 3000);
    }
  }

  // Handle messages from Plasma 6 Kity Server
  function handleServerMessage(msg) {
    switch (msg.type) {
      case 'state':
        updateUIState(msg.state, msg.text);
        break;
      case 'audioLevel':
        if (!isRecording) {
          orb.setAudioLevel(msg.level || 0);
        }
        break;
      case 'status':
        if (msg.text) {
          statusSubEl.textContent = msg.text;
        }
        break;
      case 'auth_error':
        statusSubEl.textContent = 'Error: Token no válido';
        settingsDialog.showModal();
        break;
      default:
        break;
    }
  }

  // Update Visual State (Glow, Orb, Transcript)
  function updateUIState(state, text) {
    orb.setState(state);
    screenGlow.className = `screen-glow ${state}`;

    if (state === 'listening') {
      transcriptEl.textContent = text || 'Escuchando...';
      transcriptEl.style.color = '#00e5ff';
      hintText.textContent = 'Habla ahora...';
    } else if (state === 'thinking') {
      transcriptEl.textContent = text || 'Pensando...';
      transcriptEl.style.color = '#b388ff';
      hintText.textContent = 'Kity está procesando la respuesta';
    } else if (state === 'speaking') {
      transcriptEl.textContent = text || 'Kity hablando...';
      transcriptEl.style.color = '#00e676';
      hintText.textContent = 'Reproduciendo audio en el PC';
    } else {
      transcriptEl.textContent = text || 'Toca el orbe para hablar';
      transcriptEl.style.color = 'var(--text-primary)';
      hintText.textContent = 'Pulsa para hablar o mantén pulsado';
      btnMic.classList.remove('recording');
      orb.setAudioLevel(0);
    }
  }

  // Voice Recognition (Web Speech API) & Audio RMS Analysis
  async function startRecording() {
    if (isRecording) return;
    triggerHaptic(25);

    try {
      // 1. Initialize Web Audio API for RMS meter
      if (!audioContext) {
        audioContext = new (window.AudioContext || window.webkitAudioContext)();
      }
      if (audioContext.state === 'suspended') {
        await audioContext.resume();
      }

      micStream = await navigator.mediaDevices.getUserMedia({ audio: true, video: false });
      analyser = audioContext.createAnalyser();
      analyser.fftSize = 256;
      analyser.smoothingTimeConstant = 0.4;
      const source = audioContext.createMediaStreamSource(micStream);
      source.connect(analyser);

      // Start RMS monitoring loop
      const dataArray = new Uint8Array(analyser.frequencyBinCount);
      const updateRms = () => {
        if (!isRecording) return;
        analyser.getByteFrequencyData(dataArray);
        let sum = 0;
        for (let i = 0; i < dataArray.length; i++) {
          sum += dataArray[i];
        }
        const avg = sum / dataArray.length;
        const normalizedRms = Math.min(1.0, avg / 128.0);
        orb.setAudioLevel(normalizedRms);
        rmsTimer = requestAnimationFrame(updateRms);
      };
      updateRms();

      // 2. Initialize Web Speech API
      const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
      if (SpeechRecognition) {
        recognition = new SpeechRecognition();
        recognition.lang = 'es-ES';
        recognition.continuous = true;
        recognition.interimResults = true;

        lastFinalTranscript = '';

        recognition.onstart = () => {
          isRecording = true;
          btnMic.classList.add('recording');
          updateUIState('listening');
        };

        recognition.onresult = (event) => {
          let interimTranscript = '';
          for (let i = event.resultIndex; i < event.results.length; ++i) {
            const transcript = event.results[i][0].transcript;
            if (event.results[i].isFinal) {
              lastFinalTranscript += transcript + ' ';
            } else {
              interimTranscript += transcript;
            }
          }
          const currentText = (lastFinalTranscript + interimTranscript).trim();
          if (currentText) {
            transcriptEl.textContent = currentText;
          }

          // Auto-send on silence detection if enabled
          if (autoSendToggle.checked) {
            if (speechSilenceTimer) clearTimeout(speechSilenceTimer);
            speechSilenceTimer = setTimeout(() => {
              if (isRecording && (lastFinalTranscript.trim() || interimTranscript.trim())) {
                stopRecording(true);
              }
            }, 1400);
          }
        };

        recognition.onerror = (e) => {
          console.warn('Speech recognition error:', e.error);
          if (e.error !== 'no-speech') {
            stopRecording(false);
          }
        };

        recognition.onend = () => {
          if (isRecording) {
            stopRecording(true);
          }
        };

        recognition.start();
      } else {
        // Fallback if browser lacks Web Speech API
        isRecording = true;
        btnMic.classList.add('recording');
        updateUIState('listening', 'Escuchando audio...');
      }
    } catch (err) {
      console.error('Failed to access microphone:', err);
      statusSubEl.textContent = 'Permiso de micrófono denegado';
      stopRecording(false);
    }
  }

  function stopRecording(send = true) {
    if (!isRecording) return;
    isRecording = false;
    btnMic.classList.remove('recording');
    triggerHaptic([15, 40, 15]);

    if (speechSilenceTimer) {
      clearTimeout(speechSilenceTimer);
      speechSilenceTimer = null;
    }

    if (rmsTimer) {
      cancelAnimationFrame(rmsTimer);
      rmsTimer = null;
    }

    if (micStream) {
      micStream.getTracks().forEach(track => track.stop());
      micStream = null;
    }

    if (recognition) {
      try {
        recognition.stop();
      } catch (e) {
        // Ignored
      }
      recognition = null;
    }

    const commandText = (lastFinalTranscript || transcriptEl.textContent || '').trim();

    if (send && commandText && commandText !== 'Toca el orbe para hablar' && commandText !== 'Escuchando...') {
      sendCommand(commandText);
      updateUIState('thinking', commandText);
    } else {
      updateUIState('idle');
    }
  }

  function sendCommand(text) {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      statusSubEl.textContent = 'Error: No conectado al PC';
      return;
    }

    const payload = {
      type: 'command',
      text: text,
      fromVoice: true,
      timestamp: Date.now()
    };

    ws.send(JSON.stringify(payload));
  }

  // Event Listeners
  btnMic.addEventListener('click', () => {
    if (isRecording) {
      stopRecording(true);
    } else {
      startRecording();
    }
  });

  orbWrapper.addEventListener('click', () => {
    if (isRecording) {
      stopRecording(true);
    } else {
      startRecording();
    }
  });

  // Settings Handlers
  btnSettings.addEventListener('click', () => {
    tokenInput.value = authToken;
    settingsDialog.showModal();
  });

  btnCloseSettings.addEventListener('click', () => {
    settingsDialog.close();
  });

  btnSaveSettings.addEventListener('click', () => {
    const newToken = tokenInput.value.trim();
    if (newToken) {
      authToken = newToken;
      localStorage.setItem('kity_mobile_token', authToken);
    }
    settingsDialog.close();
    if (ws) {
      ws.close();
    }
    connectWebSocket();
  });

  // Initial connection
  connectWebSocket();
})();
