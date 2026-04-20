const API = 'http://localhost:8080';

const App = { session: null, booking: null };

// ===== API HELPERS =====
async function apiPost(endpoint, body, token) {
  const headers = { 'Content-Type': 'application/json' };
  if (token) headers['Authorization'] = 'Bearer ' + token;
  const res = await fetch(API + endpoint, { method: 'POST', headers, body: JSON.stringify(body) });
  return res.json();
}
async function apiGet(endpoint, token) {
  const headers = {};
  if (token) headers['Authorization'] = 'Bearer ' + token;
  const res = await fetch(API + endpoint, { headers });
  return res.json();
}

// ===== ROUTER =====
function goTo(page) {
  document.querySelectorAll('.page').forEach(function(p) { p.classList.remove('active'); });
  document.getElementById('page-' + page).classList.add('active');
  window.scrollTo(0, 0);
  var inits = { home: initHome, connexion: initConnexion, passagers: initPassagers, sieges: initSieges, confirmation: initConfirmation };
  if (inits[page]) inits[page]();
}

// Gestion des boutons data-goto (évite les problèmes de quotes dans innerHTML)
document.addEventListener('click', function(e) {
  var btn = e.target.closest('[data-goto]');
  if (btn) goTo(btn.dataset.goto);
});

// ===== SESSION =====
function isLoggedIn() { return App.session !== null; }

function logout() { App.session = null; App.booking = null; goTo('home'); }

function updateNav(navLinksId, dark) {
  var nav = document.getElementById(navLinksId);
  if (!nav) return;
  var nameColor = dark ? 'white' : 'var(--text)';
  if (isLoggedIn()) {
    nav.innerHTML =
      '<li><button data-goto="home">Accueil</button></li>' +
      '<li><div class="user-badge">' +
        '<div class="user-avatar">' + App.session.initials + '</div>' +
        '<span class="user-name" style="color:' + nameColor + '">' + App.session.name + '</span>' +
      '</div></li>' +
      '<li><button class="btn-logout" id="btn-logout-nav">Déconnexion</button></li>';
    var logoutBtn = document.getElementById('btn-logout-nav');
    if (logoutBtn) logoutBtn.addEventListener('click', logout);
  } else {
    nav.innerHTML =
      '<li><button data-goto="home">Accueil</button></li>' +
      '<li><button class="btn-nav" data-goto="connexion">Connexion</button></li>';
  }
}

// ===== FORMAT =====
function formatDate(dateStr) {
  if (!dateStr) return '';
  var d = new Date(dateStr + 'T00:00:00');
  return d.toLocaleDateString('fr-FR', { day: 'numeric', month: 'short' });
}

function gridIdToSeatNo(gridId) {
  var parts = gridId.split('-').map(Number);
  return parts[0] * 4 + parts[1] + 1;
}
function gridIdToLabel(gridId) {
  var parts = gridId.split('-').map(Number);
  return (parts[0] + 1) + 'ABCD'[parts[1]];
}

// ===== STEPPER =====
function buildStepper(containerId, currentStep) {
  var container = document.getElementById(containerId);
  var b = App.booking || {};
  var pax = b.passagers;
  var totalPax = pax ? (pax.adult + pax.child + pax.baby) : null;
  var backSVG = '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M19 12H5M12 19l-7-7 7-7"/></svg>';

  var steps = [
    { label: 'Départ',    val: b.depart || '' },
    { label: 'Arrivée',   val: b.arrivee || '' },
    { label: 'Date',      val: b.date ? formatDate(b.date) + (b.heure ? ' ' + b.heure : '') : '' },
    { label: 'Passagers', val: totalPax ? totalPax + ' pers.' : '' }
  ];
  if (currentStep >= 3) steps.push({ label: 'Sièges', val: '' });

  var html = '';
  if (currentStep === 2) html += '<button class="btn-back" data-goto="home" title="Retour">' + backSVG + '</button>';
  if (currentStep === 3) html += '<button class="btn-back" data-goto="passagers" title="Retour">' + backSVG + '</button>';

  for (var i = 0; i < steps.length; i++) {
    if (i > 0) html += '<div class="arrow"></div>';
    var isDone   = (i + 1) < currentStep;
    var isActive = (i + 1) === currentStep;
    var cls = 'step-pill' + (isDone ? ' done' : '') + (isActive ? ' active' : '');
    html += '<div class="' + cls + '">';
    html += '<div class="step-num">' + (isDone ? '✓' : (i + 1)) + '</div>';
    html += '<span class="step-label">' + steps[i].label + '</span>';
    if (steps[i].val) html += '<span class="step-val">' + steps[i].val + '</span>';
    html += '</div>';
  }
  container.innerHTML = html;
}

// ===== PAGE HOME =====
function initHome() {
  document.getElementById('inp-date').min = new Date().toISOString().split('T')[0];
  updateNav('nav-home-links', false);
}

function swapCities() {
  var d = document.getElementById('inp-depart');
  var a = document.getElementById('inp-arrivee');
  var tmp = d.value; d.value = a.value; a.value = tmp;
  d.classList.remove('error'); a.classList.remove('error');
}

function handleReservation() {
  var depart  = document.getElementById('inp-depart').value.trim();
  var arrivee = document.getElementById('inp-arrivee').value.trim();
  var date    = document.getElementById('inp-date').value;
  var heure   = document.getElementById('inp-heure').value;
  var errEl   = document.getElementById('home-error');

  ['inp-depart','inp-arrivee','inp-date','inp-heure'].forEach(function(id) {
    document.getElementById(id).classList.remove('error');
  });
  errEl.style.display = 'none';

  var hasError = false;
  if (!depart)  { document.getElementById('inp-depart').classList.add('error');  hasError = true; }
  if (!arrivee) { document.getElementById('inp-arrivee').classList.add('error'); hasError = true; }
  if (!date)    { document.getElementById('inp-date').classList.add('error');    hasError = true; }
  if (!heure)   { document.getElementById('inp-heure').classList.add('error');   hasError = true; }

  if (!hasError && depart.toLowerCase() === arrivee.toLowerCase()) {
    errEl.textContent = "La ville de départ et d'arrivée doivent être différentes.";
    errEl.style.display = 'block';
    document.getElementById('inp-depart').classList.add('error');
    document.getElementById('inp-arrivee').classList.add('error');
    return;
  }
  if (hasError) {
    errEl.textContent = 'Veuillez remplir tous les champs avant de continuer.';
    errEl.style.display = 'block';
    return;
  }

  App.booking = { depart: depart, arrivee: arrivee, date: date, heure: heure, tripId: null, passagers: null, seats: [] };
  if (isLoggedIn()) goTo('passagers');
  else              goTo('connexion');
}

// ===== PAGE CONNEXION =====
function initConnexion() {
  var b = App.booking;
  var notice     = document.getElementById('login-notice');
  var noticeText = document.getElementById('login-notice-text');
  if (b) {
    notice.style.display = 'flex';
    noticeText.textContent = 'Connectez-vous pour réserver : ' + b.depart + ' → ' + b.arrivee + ' le ' + formatDate(b.date) + '.';
  } else {
    notice.style.display = 'none';
  }
  document.getElementById('login-error').style.display = 'none';
  document.getElementById('login-email').value    = '';
  document.getElementById('login-password').value = '';
}

async function handleLogin() {
  var email = document.getElementById('login-email').value.trim();
  var pass  = document.getElementById('login-password').value;
  var errEl = document.getElementById('login-error');
  errEl.style.display = 'none';

  if (!email || !pass) {
    errEl.textContent = 'Veuillez remplir tous les champs.';
    errEl.style.display = 'block';
    return;
  }

  var btn = document.querySelector('#page-connexion .btn-primary');
  btn.textContent = 'Connexion...';
  btn.disabled = true;

  try {
    var data = await apiPost('/api/login', { email: email, password: pass });
    if (data.status === 'OK' && data.token) {
      var name = email.split('@')[0];
      App.session = { email: email, name: name, initials: name.substring(0,2).toUpperCase(), token: data.token };
      goTo('passagers');
    } else {
      errEl.textContent = data.msg || 'Email ou mot de passe incorrect.';
      errEl.style.display = 'block';
    }
  } catch(e) {
    errEl.textContent = 'Serveur inaccessible — mode démo activé.';
    errEl.style.display = 'block';
    setTimeout(function() {
      var name = email.split('@')[0];
      App.session = { email: email, name: name, initials: name.substring(0,2).toUpperCase(), token: 'demo' };
      goTo('passagers');
    }, 1200);
  }
  btn.textContent = 'Valider';
  btn.disabled = false;
}

function togglePwd() {
  var input = document.getElementById('login-password');
  var icon  = document.getElementById('eye-icon');
  if (input.type === 'password') {
    input.type = 'text';
    icon.innerHTML = '<path d="M17.94 17.94A10.07 10.07 0 0112 20c-7 0-11-8-11-8a18.45 18.45 0 015.06-5.94M9.9 4.24A9.12 9.12 0 0112 4c7 0 11 8 11 8a18.5 18.5 0 01-2.16 3.19m-6.72-1.07a3 3 0 11-4.24-4.24"/><line x1="1" y1="1" x2="23" y2="23"/>';
  } else {
    input.type = 'password';
    icon.innerHTML = '<path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/>';
  }
}

// ===== PAGE PASSAGERS =====
var paxState = { adult: 1, child: 0, baby: 0 };

function initPassagers() {
  if (!isLoggedIn()) { goTo('connexion'); return; }
  if (!App.booking)  { goTo('home');      return; }
  updateNav('nav-passagers-links', false);
  buildStepper('stepper-passagers', 2);
  document.getElementById('passagers-subtitle').textContent =
    'Indiquez le nombre de passagers pour ' + App.booking.depart + ' → ' + App.booking.arrivee;
  paxState.adult = 1; paxState.child = 0; paxState.baby = 0;
  updatePax();
}

function change(type, delta) {
  var next = paxState[type] + delta;
  if (next < 0) return;
  if (type === 'adult' && next < 1) return;
  paxState[type] = next;
  updatePax();
}

function updatePax() {
  ['adult','child','baby'].forEach(function(t) {
    document.getElementById(t + '-val').textContent = paxState[t];
  });
  document.getElementById('adult-minus').disabled = paxState.adult <= 1;
  document.getElementById('child-minus').disabled = paxState.child <= 0;
  document.getElementById('baby-minus').disabled  = paxState.baby  <= 0;
  var total = paxState.adult + paxState.child + paxState.baby;
  document.getElementById('total-label').textContent = total + (total > 1 ? ' passagers' : ' passager');
}

function handleContinuePax() {
  App.booking.passagers = { adult: paxState.adult, child: paxState.child, baby: paxState.baby };
  goTo('sieges');
}

// ===== PAGE SIEGES =====
var ROWS = 10, COLS = 4;
var selectedSeats = new Set();
var maxSelect = 1;

async function initSieges() {
  if (!isLoggedIn()) { goTo('connexion'); return; }
  if (!App.booking)  { goTo('home');      return; }
  updateNav('nav-sieges-links', false);
  buildStepper('stepper-sieges', 3);
  var b = App.booking;
  document.getElementById('sieges-subtitle').textContent =
    'Sélectionnez vos places pour ' + b.depart + ' → ' + b.arrivee;
  var pax = b.passagers || { adult: 1, child: 0, baby: 0 };
  maxSelect = pax.adult + pax.child;
  selectedSeats.clear();

  var bookedSet = new Set();
  try {
    var from = encodeURIComponent(b.depart);
    var to   = encodeURIComponent(b.arrivee);
    var tripsData = await apiGet('/api/trips?from=' + from + '&to=' + to, App.session.token);
    if (tripsData.status === 'OK' && tripsData.trips && tripsData.trips.length > 0) {
      App.booking.tripId = tripsData.trips[0].id;
      var seatsData = await apiGet('/api/trips/' + App.booking.tripId + '/seats', App.session.token);
      if (seatsData.status === 'OK' && seatsData.seats) {
        for (var i = 0; i < seatsData.seats.length; i++) {
          if (seatsData.seats[i] === '1') bookedSet.add(i + 1);
        }
      }
    }
  } catch(e) {
    [3, 7, 12, 19, 23, 31].forEach(function(n) { bookedSet.add(n); });
  }
  buildSeatGrid(bookedSet);
  updateSeatLabel();
}

function buildSeatGrid(bookedSet) {
  var grid   = document.getElementById('seat-grid');
  grid.innerHTML = '';
  var labels = 'ABCD';
  for (var r = 0; r < ROWS; r++) {
    var row = document.createElement('div');
    row.className = 'seat-row';
    for (var c = 0; c < COLS; c++) {
      if (c === 2) {
        var aisle = document.createElement('div');
        aisle.className = 'aisle';
        row.appendChild(aisle);
      }
      var gridId = r + '-' + c;
      var seatNo = gridIdToSeatNo(gridId);
      var seat   = document.createElement('div');
      seat.className = 'seat';
      seat.textContent = (r + 1) + labels[c];
      seat.dataset.seatNo = seatNo;
      if (r === 0 && c === 0) {
        seat.classList.add('driver'); seat.textContent = '🚌';
      } else if (bookedSet.has(seatNo)) {
        seat.classList.add('taken');
      } else {
        (function(gid, el) {
          el.addEventListener('click', function() { toggleSeat(gid, el); });
        })(gridId, seat);
      }
      row.appendChild(seat);
    }
    grid.appendChild(row);
  }
}

function toggleSeat(gridId, el) {
  if (selectedSeats.has(gridId)) {
    selectedSeats.delete(gridId); el.classList.remove('selected');
  } else {
    if (selectedSeats.size >= maxSelect) return;
    selectedSeats.add(gridId); el.classList.add('selected');
  }
  updateSeatLabel();
}

function updateSeatLabel() {
  var label = document.getElementById('selected-label');
  var btn   = document.getElementById('btn-validate');
  if (selectedSeats.size === 0) {
    label.textContent = 'Aucune place sélectionnée';
    btn.disabled = true;
  } else {
    var names = Array.from(selectedSeats).map(gridIdToLabel);
    label.textContent = names.join(', ') + ' — ' + selectedSeats.size + '/' + maxSelect + ' places';
    btn.disabled = selectedSeats.size < maxSelect;
  }
}

async function handleValidate() {
  var btn = document.getElementById('btn-validate');
  btn.disabled = true;
  btn.textContent = 'Réservation...';
  App.booking.seats = Array.from(selectedSeats).map(gridIdToLabel);
  var b = App.booking;
  var allOk = true;

  if (b.tripId) {
    for (var gridId of selectedSeats) {
      var seatNo = gridIdToSeatNo(gridId);
      try {
        var data = await apiPost('/api/book', { trip_id: b.tripId, seat_no: seatNo }, App.session.token);
        if (data.status !== 'OK') {
          allOk = false;
          alert('Siège ' + gridIdToLabel(gridId) + ' : ' + (data.msg || 'erreur'));
          var seatEl = document.querySelector('[data-seat-no="' + seatNo + '"]');
          if (seatEl) { seatEl.classList.remove('selected'); seatEl.classList.add('taken'); }
          selectedSeats.delete(gridId);
        }
      } catch(e) { console.warn('API book indisponible'); }
    }
  }

  if (!allOk) {
    btn.textContent = 'Valider →'; btn.disabled = false; updateSeatLabel(); return;
  }
  goTo('confirmation');
}

// ===== PAGE CONFIRMATION =====
function initConfirmation() {
  updateNav('nav-confirm-links', false);
  var b = App.booking;
  if (!b) { goTo('home'); return; }
  var pax   = b.passagers || { adult: 1, child: 0, baby: 0 };
  var total = pax.adult + pax.child + pax.baby;
  document.getElementById('confirm-detail').innerHTML =
    '<div class="confirm-detail-row"><span>Trajet</span><span>'    + b.depart + ' → ' + b.arrivee + '</span></div>' +
    '<div class="confirm-detail-row"><span>Date</span><span>'      + formatDate(b.date) + ' à ' + b.heure + '</span></div>' +
    '<div class="confirm-detail-row"><span>Passagers</span><span>' + total + ' passager' + (total > 1 ? 's' : '') + '</span></div>' +
    '<div class="confirm-detail-row"><span>Sièges</span><span>'    + (b.seats || []).join(', ') + '</span></div>' +
    '<div class="confirm-detail-row"><span>Compte</span><span>'    + (App.session ? App.session.email : '') + '</span></div>' +
    (b.tripId ? '<div class="confirm-detail-row"><span>Réf. trajet</span><span>#' + b.tripId + '</span></div>' : '');
  App.booking = null;
}

// ===== INIT =====
initHome();
document.getElementById('login-password').addEventListener('keydown', function(e) {
  if (e.key === 'Enter') handleLogin();
});
