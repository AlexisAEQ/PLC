// Accès à l'API du serveur local.

async function request(method, url, body) {
  const res = await fetch(url, {
    method,
    headers: body ? { 'Content-Type': 'application/json' } : {},
    body: body ? JSON.stringify(body) : undefined,
  });
  let data = null;
  try {
    data = await res.json();
  } catch {
    /* corps vide */
  }
  if (!res.ok) throw new Error(data?.error || `HTTP ${res.status}`);
  return data;
}

export const api = {
  info: () => request('GET', '/api/info'),
  hardware: () => request('GET', '/api/hardware'),
  listProjects: () => request('GET', '/api/projects'),
  getProject: (name) => request('GET', `/api/projects/${encodeURIComponent(name)}`),
  saveProject: (project) => request('PUT', `/api/projects/${encodeURIComponent(project.name)}`, project),
  preview: (project) => request('POST', '/api/preview', { project }),
  apply: (project, files, target) => request('POST', '/api/apply', { project, files, target }),
};
