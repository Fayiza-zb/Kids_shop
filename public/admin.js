const $ = id => document.getElementById(id);
const esc = s => String(s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
let token = ''; try { token = sessionStorage.getItem('token') || ''; } catch (e) {}
let products = [];

async function api(path, data) {
  const opt = { headers: { 'X-Token': token } };
  if (data) { opt.method = 'POST'; opt.headers['Content-Type'] = 'application/x-www-form-urlencoded'; opt.body = new URLSearchParams(data); }
  const r = await fetch(path, opt); const d = await r.json();
  if (r.status === 401 && token) { signOut(); }
  if (!r.ok) throw new Error(d.error || 'Request failed');
  return d;
}
function say(t, ok = true) { $('msg').innerHTML = t ? `<div class="msg ${ok ? 'ok' : 'err'}">${esc(t)}</div>` : ''; }
function signOut() { token = ''; try { sessionStorage.removeItem('token'); } catch (e) {} $('panel').hidden = true; $('login').hidden = false; $('logout').hidden = true; }
function enter() { $('login').hidden = true; $('panel').hidden = false; $('logout').hidden = false; loadProducts(); loadOrders(); }

$('go').onclick = async () => {
  try {
    const d = await api('/api/admin/login', { password: $('pw').value });
    token = d.token; try { sessionStorage.setItem('token', token); } catch (e) {}
    $('pw').value = ''; $('lmsg').innerHTML = ''; enter();
  } catch (e) { $('lmsg').innerHTML = `<div class="msg err">${esc(e.message)}</div>`; }
};
$('logout').onclick = signOut;

function tab(name) {
  $('products').hidden = name !== 'products'; $('orders').hidden = name !== 'orders';
  $('tProducts').className = 'btn' + (name === 'products' ? '' : ' ghost');
  $('tOrders').className = 'btn' + (name === 'orders' ? '' : ' ghost');
}
$('tProducts').onclick = () => tab('products');
$('tOrders').onclick = () => { tab('orders'); loadOrders(); };

async function loadProducts() {
  products = await (await fetch('/api/products')).json();
  $('cats').innerHTML = [...new Set(products.map(p => p.category))].map(c => `<option value="${esc(c)}">`).join('');
  $('ptable').innerHTML = products.map(p => `<tr>
    <td>${esc(p.icon)}</td><td>${esc(p.name)}</td><td>${esc(p.category)}</td>
    <td>₹${p.price.toFixed(2)}</td><td>${p.stock}</td>
    <td><button class="btn small ghost" data-e="${p.id}">Edit</button>
        <button class="btn small ghost" data-d="${p.id}">Delete</button></td></tr>`).join('');
  document.querySelectorAll('[data-e]').forEach(b => b.onclick = () => edit(+b.dataset.e));
  document.querySelectorAll('[data-d]').forEach(b => b.onclick = () => del(+b.dataset.d));
}
function edit(id) {
  const p = products.find(x => x.id === id);
  $('pid').value = p.id; $('pname').value = p.name; $('pcat').value = p.category;
  $('pprice').value = p.price; $('pstock').value = p.stock; $('picon').value = p.icon;
  window.scrollTo({ top: 0 });
}
function clearForm() { ['pid','pname','pcat','pprice','pstock','picon'].forEach(i => $(i).value = i === 'pid' ? '0' : ''); }
$('clear').onclick = clearForm;
$('save').onclick = async () => {
  try {
    await api('/api/admin/product-save', { id: $('pid').value, name: $('pname').value, category: $('pcat').value,
      price: $('pprice').value, stock: $('pstock').value, icon: $('picon').value });
    say('Product saved.'); clearForm(); loadProducts();
  } catch (e) { say(e.message, false); }
};
async function del(id) {
  if (!confirm('Delete this product?')) return;
  try { await api('/api/admin/product-delete', { id }); say('Product deleted.'); loadProducts(); }
  catch (e) { say(e.message, false); }
}

async function loadOrders() {
  try {
    const list = await api('/api/admin/orders');
    $('otable').innerHTML = list.length ? list.map(o => `<tr>
      <td>${o.id}<br><small>${esc(o.date)}</small></td>
      <td>${esc(o.name)}<br>${esc(o.phone)}<br><small>${esc(o.address)}</small></td>
      <td>${esc(o.items)}</td><td>₹${o.total.toFixed(2)}</td>
      <td><select data-o="${o.id}">${['Pending','Shipped','Delivered','Cancelled'].map(s =>
        `<option ${s === o.status ? 'selected' : ''}>${s}</option>`).join('')}</select></td></tr>`).join('')
      : '<tr><td colspan="5" class="empty">No orders yet. New orders appear here.</td></tr>';
    document.querySelectorAll('[data-o]').forEach(s => s.onchange = async () => {
      try { await api('/api/admin/order-status', { id: s.dataset.o, status: s.value }); say('Order status updated.'); }
      catch (e) { say(e.message, false); }
    });
  } catch (e) { say(e.message, false); }
}

if (token) enter();
