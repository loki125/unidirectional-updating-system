document.addEventListener('DOMContentLoaded', () => {
    // Tab switching logic
    const tabs = document.querySelectorAll('.nav-links li');
    const contents = document.querySelectorAll('.tab-content');

    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            tabs.forEach(t => t.classList.remove('active'));
            contents.forEach(c => c.classList.remove('active'));
            
            tab.classList.add('active');
            document.getElementById(tab.dataset.tab).classList.add('active');
        });
    });

    // --- Infrastructure Section Logic ---
    loadInfrastructure();

    async function loadInfrastructure() {
        try {
            const res = await fetch('/api/networks');
            const networks = await res.json();
            renderNetworkTree(networks);
        } catch (err) {
            console.error('Failed to load infrastructure', err);
            document.getElementById('network-tree').innerHTML = '<div class="empty-state">Failed to load infrastructure data.</div>';
        }
    }

    function renderNetworkTree(networks) {
        const tree = document.getElementById('network-tree');
        tree.innerHTML = '';

        if (networks.length === 0) {
            tree.innerHTML = '<div class="empty-state">No networks found.</div>';
            return;
        }

        networks.forEach(net => {
            const item = document.createElement('div');
            item.className = 'tree-item';
            
            const header = document.createElement('div');
            header.className = 'tree-header';
            header.innerHTML = `<span>${net.name}</span><span class="toggle-icon">+</span>`;
            
            const children = document.createElement('ul');
            children.className = 'tree-children';

            net.computers.forEach(comp => {
                const child = document.createElement('li');
                child.className = 'tree-child';
                child.textContent = comp.pc_username;
                
                child.addEventListener('click', (e) => {
                    e.stopPropagation();
                    selectItem(child);
                    loadPackages('computer', comp.id, comp.pc_username);
                });
                
                children.appendChild(child);
            });

            header.addEventListener('click', () => {
                item.classList.toggle('expanded');
                const icon = header.querySelector('.toggle-icon');
                icon.textContent = item.classList.contains('expanded') ? '-' : '+';
                selectItem(header);
                loadPackages('network', net.id, net.name);
            });

            item.appendChild(header);
            item.appendChild(children);
            tree.appendChild(item);
        });
    }

    function selectItem(element) {
        document.querySelectorAll('.tree-header, .tree-child').forEach(el => el.classList.remove('selected'));
        element.classList.add('selected');
    }

    async function loadPackages(type, id, name) {
        const details = document.getElementById('infra-details');
        const header = document.getElementById('details-header');
        
        header.textContent = `Packages connected to ${name}`;
        details.innerHTML = '<div class="empty-state">Loading packages...</div>';

        try {
            const res = await fetch(`/api/${type}s/${id}/packages`);
            const packages = await res.json();
            
            if (packages.length === 0) {
                details.innerHTML = `<div class="empty-state">No packages connected to this ${type}.</div>`;
                return;
            }

            let html = '';
            packages.forEach(pkg => {
                let healthJson = '{}';
                try {
                    healthJson = JSON.stringify(JSON.parse(pkg.health), null, 2);
                } catch(e) {
                    healthJson = pkg.health;
                }

                html += `
                    <div class="package-card">
                        <h3>${pkg.name}</h3>
                        <div class="meta">
                            <span><strong>Version</strong> ${pkg.version}</span>
                            <span><strong>Architecture</strong> ${pkg.arch}</span>
                        </div>
                        <div class="section-label">Health Data</div>
                        <div class="json-block mono">${healthJson}</div>
                    </div>
                `;
            });
            details.innerHTML = html;

        } catch (err) {
            console.error('Failed to load packages', err);
            details.innerHTML = '<div class="empty-state">Error loading packages.</div>';
        }
    }

    // --- Packages Section Logic ---
    const packageSearch = document.getElementById('package-search');
    let searchTimeout;

    packageSearch.addEventListener('input', (e) => {
        clearTimeout(searchTimeout);
        searchTimeout = setTimeout(() => {
            loadAllPackages(e.target.value);
        }, 300);
    });

    loadAllPackages();

    async function loadAllPackages(query = '') {
        const grid = document.getElementById('package-grid');
        
        // Setup header
        grid.innerHTML = `
            <div class="grid-header">
                <span>Name</span>
                <span>Version</span>
                <span>Architecture</span>
                <span>Metadata</span>
            </div>
            <div class="grid-body" id="grid-body">
                <div class="empty-state">Loading...</div>
            </div>
        `;
        
        const gridBody = document.getElementById('grid-body');

        try {
            const url = query ? `/api/packages?search=${encodeURIComponent(query)}` : '/api/packages';
            const res = await fetch(url);
            const packages = await res.json();

            if (packages.length === 0) {
                gridBody.innerHTML = '<div class="empty-state">No packages found matching your search.</div>';
                return;
            }

            gridBody.innerHTML = '';
            packages.forEach(pkg => {
                let metaJson = '{}';
                try {
                    metaJson = JSON.stringify(JSON.parse(pkg.metadata_json), null, 2);
                } catch(e) {
                    metaJson = pkg.metadata_json;
                }

                const row = document.createElement('div');
                row.className = 'grid-row';
                row.innerHTML = `
                    <div><strong>${pkg.name}</strong></div>
                    <div>${pkg.version}</div>
                    <div>${pkg.arch}</div>
                    <div class="json-block mono" style="margin: 0; padding: 8px; font-size: 11px;">${metaJson}</div>
                `;
                gridBody.appendChild(row);
            });
        } catch (err) {
            console.error('Failed to load all packages', err);
            gridBody.innerHTML = '<div class="empty-state">Error loading packages.</div>';
        }
    }
});
