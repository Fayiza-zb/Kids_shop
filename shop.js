const $ = id => document.getElementById(id);
const money = n => '₹' + Number(n).toLocaleString('en-IN', {minimumFractionDigits: 2, maximumFractionDigits: 2});
const esc = s => String(s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));

let products = [], category = 'All', query = '';
let cart = {};
try { cart = JSON.parse(localStorage.getItem('cart') || '{}'); } catch (e) { cart = {}; }

function saveCart() { try { localStorage.setItem('cart', JSON.stringify(cart)); } catch (e) {} }

async function loadProducts() {
  const r = await fetch('/api/products');
  products = await r.json();
  for (const id of Object.keys(cart)) if (!products.find(p => p.id == id)) delete cart[id];
  renderChips(); renderGrid(); renderCart();
}

function renderChips() {
  const cats = ['All', ...new Set(products.map(p => p.category))];
  $('chips').innerHTML = cats.map(c =>
    `<button class="chip" aria-pressed="${c === category}" data-c="${esc(c)}">${esc(c)}</button>`).join('');
  $('chips').querySelectorAll('.chip').forEach(b => b.onclick = () => { category = b.dataset.c; renderChips(); renderGrid(); });
}

function renderGrid() {
  const list = products.filter(p =>
    (category === 'All' || p.category === category) &&
    (p.name + ' ' + p.category).toLowerCase().includes(query));
  if (!list.length) { $('grid').innerHTML = '<p class="empty">No products match. Try a different word or category.</p>'; return; }
  $('grid').innerHTML = list.map(p => `
    <article class="card">
      <div class="pic" aria-hidden="true">${esc(p.icon)}</div>
      <h3>${esc(p.name)}</h3>
      <span class="cat">${esc(p.category)}</span>
      <div class="row">
        <div><div class="price">${money(p.price)}</div>
        <div class="stock ${p.stock < 6 ? 'low' : ''}">${p.stock ? (p.stock < 6 ? 'Only ' + p.stock + ' left' : 'In stock') : 'Out of stock'}</div></div>
        <button class="btn small" data-id="${p.id}" ${p.stock ? '' : 'disabled'}>Add</button>
      </div>
    </article>`).join('');
  $('grid').querySelectorAll('button[data-id]').forEach(b => b.onclick = () => change(b.dataset.id, 1));
}

function change(id, d) {
  const p = products.find(x => x.id == id); if (!p) return;
  const q = (cart[id] || 0) + d;
  if (q <= 0) delete cart[id]; else cart[id] = Math.min(q, p.stock);
  saveCart(); renderCart();
}

function renderCart() {
  const ids = Object.keys(cart); let total = 0, count = 0;
  $('lines').innerHTML = ids.length ? ids.map(id => {
    const p = products.find(x => x.id == id); total += p.price * cart[id]; count += cart[id];
    return `<div class="line"><span class="ic">${esc(p.icon)}</span>
      <div><div>${esc(p.name)}</div><div class="cat">${money(p.price)} each</div></div>
      <div class="qty"><button data-id="${id}" data-d="-1" aria-label="Remove one">−</button>${cart[id]}
      <button data-id="${id}" data-d="1" aria-label="Add one">+</button></div></div>`;
  }).join('') : '<p class="empty">Your cart is empty. Add something from the shop.</p>';
  $('lines').querySelectorAll('button[data-id]').forEach(b => b.onclick = () => change(b.dataset.id, +b.dataset.d));
  $('total').textContent = money(total);
  $('count').textContent = count;
  $('checkout').style.display = ids.length ? '' : 'none';
}

function show(text, ok) { $('msg').innerHTML = text ? `<div class="msg ${ok ? 'ok' : 'err'}">${esc(text)}</div>` : ''; }

async function placeOrder() {
  const items = Object.entries(cart).map(([id, q]) => id + ':' + q).join(',');
  const body = new URLSearchParams({ name: $('cname').value, phone: $('cphone').value, address: $('caddr').value, items });
  $('place').disabled = true;
  try {
    const r = await fetch('/api/order', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body });
    const d = await r.json();
    if (!r.ok) { show(d.error || 'Could not place order.', false); }
    else { cart = {}; saveCart(); show(`Order #${d.orderId} placed. Total ${money(d.total)}. Thank you!`, true); await loadProducts(); }
  } catch (e) { show('Cannot reach the server. Is it running?', false); }
  $('place').disabled = false;
}

$('search').oninput = e => { query = e.target.value.trim().toLowerCase(); renderGrid(); };
$('openCart').onclick = () => { $('drawer').classList.add('open'); show('', true); };
$('closeCart').onclick = () => $('drawer').classList.remove('open');
$('place').onclick = placeOrder;
loadProducts();
