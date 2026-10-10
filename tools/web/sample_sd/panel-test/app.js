fetch('/status', { cache: 'no-store' })
  .then(response => {
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  })
  .then(status => {
    document.querySelector('#status').textContent =
      `microSD montada: ${status.sd_mounted ? 'sí' : 'no'} · página: ${status.page_version}`;
  })
  .catch(() => {
    document.querySelector('#status').textContent = 'No se pudo consultar /status.';
  });
