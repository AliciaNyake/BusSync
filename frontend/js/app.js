const API = 'http://localhost:8080/api';

// ===== API HELPERS =====
async function apiPost(endpoint, body, token) {
  const headers = { 'Content-Type': 'application/json' };

  if (token) {
    headers['Authorization'] = 'Bearer ' + token;
  }

  const res = await fetch(API + endpoint, {
    method: 'POST',
    headers,
    body: JSON.stringify(body)
  });

  const text = await res.text();
  console.log('apiPost raw response:', text);

  try {
    return JSON.parse(text);
  } catch (e) {
    throw new Error('Réponse serveur invalide: ' + text);
  }
}

async function apiGet(endpoint, token) {
  const headers = {};

  if (token) {
    headers['Authorization'] = 'Bearer ' + token;
  }

  const res = await fetch(API + endpoint, {
    method: 'GET',
    headers
  });

  const text = await res.text();
  console.log('apiGet raw response:', text);

  try {
    return JSON.parse(text);
  } catch (e) {
    throw new Error('Réponse serveur invalide: ' + text);
  }
}

// ===== STEPPER =====
function buildStepper(containerId, booking, currentStep) {
  var container = document.getElementById(containerId);
  if (!container) return;

  var b = booking || {};
  var pax = b.passagers;
  var totalPax = pax ? (pax.adult + pax.child + pax.baby) : null;

  var backSVG =
    '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5">' +
    '<path d="M19 12H5M12 19l-7-7 7-7"/>' +
    '</svg>';

  var steps = [
    { label: 'Départ', val: b.depart || '' },
    { label: 'Arrivée', val: b.arrivee || '' },
    { label: 'Date', val: b.date ? formatDate(b.date) + (b.heure ? ' ' + b.heure : '') : '' },
    { label: 'Passagers', val: totalPax ? totalPax + ' pers.' : '' }
  ];

  if (currentStep >= 3) {
    steps.push({ label: 'Sièges', val: '' });
  }

  var html = '';

  if (currentStep === 2) {
    html += '<button class="btn-back" onclick="window.location.href=\'../index.html\'" title="Retour">' + backSVG + '</button>';
  }

  if (currentStep === 3) {
    html += '<button class="btn-back" onclick="window.location.href=\'passagers.html\'" title="Retour">' + backSVG + '</button>';
  }

  for (var i = 0; i < steps.length; i++) {
    if (i > 0) {
      html += '<div class="arrow"></div>';
    }

    var isDone = (i + 1) < currentStep;
    var isActive = (i + 1) === currentStep;
    var cls = 'step-pill' + (isDone ? ' done' : '') + (isActive ? ' active' : '');

    html += '<div class="' + cls + '">';
    html += '<div class="step-num">' + (isDone ? '✓' : (i + 1)) + '</div>';
    html += '<span class="step-label">' + steps[i].label + '</span>';

    if (steps[i].val) {
      html += '<span class="step-val">' + steps[i].val + '</span>';
    }

    html += '</div>';
  }

  container.innerHTML = html;
}

// ===== FORMAT =====
function formatDate(dateStr) {
  if (!dateStr) return '';

  var d = new Date(dateStr + 'T00:00:00');

  return d.toLocaleDateString('fr-FR', {
    day: 'numeric',
    month: 'short'
  });
}