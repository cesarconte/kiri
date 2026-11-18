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
  const sttEngineSelect = document.getElementById('sttEngineSelect');
  const hapticToggle = document.getElementById('hapticToggle');
  const autoSendToggle = document.getElementById('autoSendToggle');
  const hintText = document.getElementById('hintText');
  const modeAssistantBtn = document.getElementById('modeAssistantBtn');
  const modeDictationBtn = document.getElementById('modeDictationBtn');

  // State
  let ws = null;
  let isRecording = false;
  let audioContext = null;
  let analyser = null;
  let micStream = null;
  let mediaRecorder = null;
  let audioChunks = [];
  let recognition = null;
  let reconnectTimer = null;
  let rmsTimer = null;
  let lastFinalTranscript = '';
  let currentInterimTranscript = '';
  let sttMode = localStorage.getItem('kity_mobile_stt_mode') || 'pc';
  let targetMode = localStorage.getItem('kity_mobile_target_mode') || 'assistant';
  let speechSilenceTimer = null;
  let vadSilenceTimer = null;
  let maxRecordingTimer = null;

  // Initialize Siri Orb Canvas
  const orb = new window.SiriOrb(orbCanvas);
  orb.start();

  const DEFAULT_AUTH_TOKEN = '2137a6f75d4b5d5d84f64776f3f24c718802eb863bcaf397314061ac6793e163';

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
    if (window.KITY_AUTH_TOKEN) {
      localStorage.setItem('kity_mobile_token', window.KITY_AUTH_TOKEN);
      return window.KITY_AUTH_TOKEN;
    }
    const stored = localStorage.getItem('kity_mobile_token');
    if (stored && stored.length >= 32) {
      return stored;
    }
    localStorage.setItem('kity_mobile_token', DEFAULT_AUTH_TOKEN);
    return DEFAULT_AUTH_TOKEN;
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
        if (authToken !== DEFAULT_AUTH_TOKEN) {
          authToken = DEFAULT_AUTH_TOKEN;
          localStorage.setItem('kity_mobile_token', authToken);
          statusSubEl.textContent = 'Actualizando token de seguridad...';
          if (ws) ws.close();
          connectWebSocket();
        } else {
          statusSubEl.textContent = 'Error: Token no válido';
          settingsDialog.showModal();
        }
        break;
      default:
        break;
    }
  }

  function updateTargetMode(mode) {
    targetMode = mode;
    localStorage.setItem('kity_mobile_target_mode', mode);
    if (mode === 'dictation') {
      if (modeAssistantBtn) {
        modeAssistantBtn.classList.remove('active');
        modeAssistantBtn.setAttribute('aria-selected', 'false');
      }
      if (modeDictationBtn) {
        modeDictationBtn.classList.add('active');
        modeDictationBtn.setAttribute('aria-selected', 'true');
      }
      if (!isRecording) {
        transcriptEl.textContent = 'Toca para dictar en el PC';
        hintText.textContent = '✍️ Lo que digas se escribirá en el cursor del PC';
      }
    } else {
      if (modeDictationBtn) {
        modeDictationBtn.classList.remove('active');
        modeDictationBtn.setAttribute('aria-selected', 'false');
      }
      if (modeAssistantBtn) {
        modeAssistantBtn.classList.add('active');
        modeAssistantBtn.setAttribute('aria-selected', 'true');
      }
      if (!isRecording) {
        transcriptEl.textContent = 'Toca el orbe para hablar';
        hintText.textContent = 'Pulsa para hablar o mantén pulsado';
      }
    }
  }

  if (modeAssistantBtn && modeDictationBtn) {
    modeAssistantBtn.addEventListener('click', () => {
      triggerHaptic(14);
      updateTargetMode('assistant');
    });
    modeDictationBtn.addEventListener('click', () => {
      triggerHaptic(14);
      updateTargetMode('dictation');
    });
    updateTargetMode(targetMode);
  }

  // Update Visual State (Glow, Orb, Transcript)
  function updateUIState(state, text) {
    orb.setState(state);
    screenGlow.className = `screen-glow ${state}`;

    if (state === 'listening') {
      transcriptEl.textContent = text || (targetMode === 'dictation' ? 'Dictando al PC...' : 'Escuchando...');
      transcriptEl.style.color = targetMode === 'dictation' ? '#00e676' : '#00e5ff';
      hintText.textContent = targetMode === 'dictation' ? 'Habla para escribir en el PC...' : 'Habla ahora...';
    } else if (state === 'thinking') {
      transcriptEl.textContent = text || (targetMode === 'dictation' ? 'Insertando en el cursor…' : 'Pensando...');
      transcriptEl.style.color = targetMode === 'dictation' ? '#00e676' : '#b388ff';
      hintText.textContent = targetMode === 'dictation' ? 'KDictation procesando audio' : 'Kity está procesando la respuesta';
    } else if (state === 'speaking') {
      transcriptEl.textContent = text || 'Kity hablando...';
      transcriptEl.style.color = '#00e676';
      hintText.textContent = 'Reproduciendo audio en el PC';
    } else {
      if (text) {
        transcriptEl.textContent = text;
      } else {
        transcriptEl.textContent = targetMode === 'dictation' ? 'Toca para dictar en el PC' : 'Toca el orbe para hablar';
      }
      transcriptEl.style.color = 'var(--text-primary)';
      hintText.textContent = targetMode === 'dictation' ? '✍️ Lo que digas se escribirá en el cursor del PC' : 'Pulsa para hablar o mantén pulsado';
      btnMic.classList.remove('recording');
      orb.setAudioLevel(0);
    }
  }

  // Secure Context Modal Elements
  const secureNoticeDialog = document.getElementById('secureNoticeDialog');
  const btnCloseSecureNotice = document.getElementById('btnCloseSecureNotice');
  const btnDismissSecureNotice = document.getElementById('btnDismissSecureNotice');
  const originUrlInput = document.getElementById('originUrlInput');
  const btnCopyOrigin = document.getElementById('btnCopyOrigin');

  function showSecureContextNotice() {
    const origin = window.location.protocol + '//' + window.location.host;
    if (originUrlInput) {
      originUrlInput.value = origin;
    }
    if (secureNoticeDialog) {
      secureNoticeDialog.showModal();
    }
  }

  if (btnCloseSecureNotice) {
    btnCloseSecureNotice.addEventListener('click', () => secureNoticeDialog.close());
  }
  if (btnDismissSecureNotice) {
    btnDismissSecureNotice.addEventListener('click', () => secureNoticeDialog.close());
  }
  if (btnCopyOrigin) {
    btnCopyOrigin.addEventListener('click', () => {
      if (originUrlInput) {
        originUrlInput.select();
        if (navigator.clipboard && navigator.clipboard.writeText) {
          navigator.clipboard.writeText(originUrlInput.value);
        } else {
          document.execCommand('copy');
        }
        btnCopyOrigin.textContent = '¡Copiado!';
        setTimeout(() => { btnCopyOrigin.textContent = 'Copiar'; }, 2000);
      }
    });
  }

  // Voice Recognition & Audio Analysis
  async function startRecording() {
    if (isRecording) return;

    if (!window.isSecureContext || !navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
      showSecureContextNotice();
      return;
    }

    triggerHaptic(25);

    try {
      // 1. Initialize Web Audio API for RMS meter
      if (!audioContext) {
        audioContext = new (window.AudioContext || window.webkitAudioContext)();
      }
      if (audioContext.state === 'suspended') {
        await audioContext.resume();
      }

      // Hardware acoustic filters: echoCancellation, noiseSuppression, autoGainControl
      micStream = await navigator.mediaDevices.getUserMedia({
        audio: {
          echoCancellation: true,
          noiseSuppression: true,
          autoGainControl: true,
          channelCount: 1
        },
        video: false
      });

      analyser = audioContext.createAnalyser();
      analyser.fftSize = 256;
      analyser.smoothingTimeConstant = 0.4;
      const source = audioContext.createMediaStreamSource(micStream);
      source.connect(analyser);

      // Start RMS monitoring loop and Voice Activity Detection (VAD)
      const dataArray = new Uint8Array(analyser.frequencyBinCount);
      let speechDetected = false;
      let speechFrames = 0;
      if (vadSilenceTimer) {
        clearTimeout(vadSilenceTimer);
        vadSilenceTimer = null;
      }
      if (maxRecordingTimer) {
        clearTimeout(maxRecordingTimer);
      }
      maxRecordingTimer = setTimeout(() => {
        if (isRecording) {
          stopRecording(true);
        }
      }, 30000);

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

        // VAD: detect voice activity
        if (normalizedRms > 0.07) {
          speechFrames++;
          if (speechFrames >= 3) {
            speechDetected = true;
            if (sttMode === 'pc') {
              transcriptEl.textContent = 'Grabando voz... (toca para enviar)';
            } else if (!lastFinalTranscript && transcriptEl.textContent.startsWith('Escuchando')) {
              transcriptEl.textContent = 'Escuchando voz... (toca para enviar)';
            }
          }
          if (vadSilenceTimer) {
            clearTimeout(vadSilenceTimer);
            vadSilenceTimer = null;
          }
        } else if (speechDetected && autoSendToggle.checked) {
          // Generous 2.5s silence timeout allows natural speaking pace and pauses
          if (!vadSilenceTimer) {
            vadSilenceTimer = setTimeout(() => {
              if (isRecording) {
                stopRecording(true);
              }
            }, 2500);
          }
        }

        rmsTimer = requestAnimationFrame(updateRms);
      };
      updateRms();

      // 2. Initialize MediaRecorder for high-fidelity audio capture
      audioChunks = [];
      try {
        let mimeType = 'audio/webm;codecs=opus';
        if (typeof MediaRecorder !== 'undefined') {
          if (!MediaRecorder.isTypeSupported('audio/webm;codecs=opus')) {
            if (MediaRecorder.isTypeSupported('audio/webm')) mimeType = 'audio/webm';
            else if (MediaRecorder.isTypeSupported('audio/mp4')) mimeType = 'audio/mp4';
            else if (MediaRecorder.isTypeSupported('audio/ogg')) mimeType = 'audio/ogg';
          }
          mediaRecorder = new MediaRecorder(micStream, { mimeType: mimeType });
          mediaRecorder.ondataavailable = (e) => {
            if (e.data && e.data.size > 0) {
              audioChunks.push(e.data);
            }
          };
          mediaRecorder.start(100);
        }
      } catch (recErr) {
        console.warn('MediaRecorder error:', recErr);
      }

      isRecording = true;
      btnMic.classList.add('recording');
      lastFinalTranscript = '';
      currentInterimTranscript = '';

      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ type: 'recording_start' }));
      }

      // 3. Initialize Web Speech API ONLY if webspeech mode is selected
      // (avoids concurrent microphone contention on Android HAL when in PC mode)
      if (sttMode === 'webspeech') {
        updateUIState('listening', 'Escuchando en el móvil…');
        const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
        if (SpeechRecognition) {
          recognition = new SpeechRecognition();
          recognition.lang = 'es-ES';
          recognition.continuous = true;
          recognition.interimResults = true;

          recognition.onresult = (event) => {
            let interim = '';
            for (let i = event.resultIndex; i < event.results.length; ++i) {
              const transcript = event.results[i][0].transcript;
              if (event.results[i].isFinal) {
                lastFinalTranscript += transcript + ' ';
              } else {
                interim += transcript;
              }
            }
            currentInterimTranscript = interim;
            const currentText = (lastFinalTranscript + currentInterimTranscript).trim();
            if (currentText) {
              transcriptEl.textContent = currentText;
            }

            // Auto-send on silence detection if enabled
            if (autoSendToggle.checked) {
              if (speechSilenceTimer) clearTimeout(speechSilenceTimer);
              speechSilenceTimer = setTimeout(() => {
                if (isRecording && (lastFinalTranscript.trim() || currentInterimTranscript.trim())) {
                  stopRecording(true);
                }
              }, 2500);
            }
          };

          recognition.onerror = (e) => {
            console.warn('Speech recognition warning:', e.error);
          };

          try {
            recognition.start();
          } catch (e) {
            console.warn('Recognition start error:', e);
          }
        }
      } else {
        updateUIState('listening', 'Escuchando… (procesará en el PC)');
      }
    } catch (err) {
      console.error('Failed to access microphone:', err);
      statusSubEl.textContent = 'Permiso de micrófono denegado';
      stopRecording(false);
      showSecureContextNotice();
    }
  }

  function stopRecording(send = true) {
    if (!isRecording) return;
    isRecording = false;
    btnMic.classList.remove('recording');
    triggerHaptic([15, 40, 15]);

    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ type: 'recording_stop' }));
    }

    if (speechSilenceTimer) {
      clearTimeout(speechSilenceTimer);
      speechSilenceTimer = null;
    }

    if (vadSilenceTimer) {
      clearTimeout(vadSilenceTimer);
      vadSilenceTimer = null;
    }

    if (maxRecordingTimer) {
      clearTimeout(maxRecordingTimer);
      maxRecordingTimer = null;
    }

    if (rmsTimer) {
      cancelAnimationFrame(rmsTimer);
      rmsTimer = null;
    }

    if (recognition) {
      try {
        recognition.stop();
      } catch (e) {
        // Ignored
      }
      recognition = null;
    }

    const deliverPayload = () => {
      if (!send) {
        updateUIState('idle');
        return;
      }

      // If user selected mobile Web Speech recognition:
      if (sttMode === 'webspeech') {
        const fullTranscript = (lastFinalTranscript + ' ' + (currentInterimTranscript || '')).trim();
        if (fullTranscript) {
          sendCommand(fullTranscript);
          updateUIState('thinking', fullTranscript);
          if (micStream) {
            micStream.getTracks().forEach(track => track.stop());
            micStream = null;
          }
          return;
        }
      }

      // Default PC mode (or fallback if Web Speech yielded no text):
      if (audioChunks.length > 0) {
        updateUIState('thinking', 'Transcribiendo audio en PC (Whisper)…');
        const mime = (mediaRecorder && mediaRecorder.mimeType) ? mediaRecorder.mimeType : 'audio/webm';
        const audioBlob = new Blob(audioChunks, { type: mime });
        const reader = new FileReader();
        reader.onloadend = () => {
          const parts = reader.result.split(',');
          const base64Data = parts.length > 1 ? parts[1] : parts[0];
          if (base64Data && ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({
              type: 'audio',
              mode: targetMode,
              data: base64Data,
              format: 'webm'
            }));
          }
        };
        reader.readAsDataURL(audioBlob);
      } else {
        updateUIState('idle');
      }

      if (micStream) {
        micStream.getTracks().forEach(track => track.stop());
        micStream = null;
      }
    };

    if (mediaRecorder && mediaRecorder.state !== 'inactive') {
      mediaRecorder.onstop = () => {
        deliverPayload();
      };
      try {
        mediaRecorder.stop();
      } catch (e) {
        deliverPayload();
      }
    } else {
      deliverPayload();
    }
  }

  function sendCommand(text) {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      statusSubEl.textContent = 'Error: No conectado al PC';
      return;
    }

    const payload = {
      type: 'command',
      mode: targetMode,
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
    if (sttEngineSelect) {
      sttEngineSelect.value = sttMode;
    }
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
    if (sttEngineSelect) {
      sttMode = sttEngineSelect.value;
      localStorage.setItem('kity_mobile_stt_mode', sttMode);
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
