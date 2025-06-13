document.addEventListener('DOMContentLoaded', () => {
    const groupList = document.getElementById('groupList');
    const newGroupNameInput = document.getElementById('newGroupName');
    const newGroupDescriptionInput = document.getElementById('newGroupDescription');
    const createGroupBtn = document.getElementById('createGroupBtn');
    const toggleDebugBtn = document.getElementById('toggleDebugBtn');
    const debugInfo = document.getElementById('debugInfo');

    const groupDetailsSection = document.getElementById('groupDetails');
    const detailGroupName = document.getElementById('detailGroupName');
    const detailGroupId = document.getElementById('detailGroupId');
    const detailGroupDescription = document.getElementById('detailGroupDescription');
    const detailGroupPriority = document.getElementById('detailGroupPriority');

    const updateDescriptionInput = document.getElementById('updateDescriptionInput');
    const updateDescriptionBtn = document.getElementById('updateDescriptionBtn');
    const updatePriorityInput = document.getElementById('updatePriorityInput');
    const updatePriorityBtn = document.getElementById('updatePriorityBtn');

    const addPermissionInput = document.getElementById('addPermissionInput');
    const addPermissionBtn = document.getElementById('addPermissionBtn');
    const removePermissionInput = document.getElementById('removePermissionInput');
    const removePermissionBtn = document.getElementById('removePermissionBtn');
    const directPermissionsList = document.getElementById('directPermissionsList');
    const effectivePermissionsList = document.getElementById('effectivePermissionsList');

    const addParentInput = document.getElementById('addParentInput');
    const addParentBtn = document.getElementById('addParentBtn');
    const removeParentInput = document.getElementById('removeParentInput');
    const removeParentBtn = document.getElementById('removeParentBtn');
    const parentGroupsList = document.getElementById('parentGroupsList');

    const addPlayerInput = document.getElementById('addPlayerInput');
    const addPlayerBtn = document.getElementById('addPlayerBtn');
    const removePlayerInput = document.getElementById('removePlayerInput');
    const removePlayerBtn = document.getElementById('removePlayerBtn');
    const playersInGroupList = document.getElementById('playersInGroupList');

    const deleteGroupBtn = document.getElementById('deleteGroupBtn');

    let currentGroup = null; // 当前选中的组名

    // 辅助函数：显示消息
    function showMessage(message, isError = false) {
        alert(message); // 简单的 alert，实际应用中可以用更友好的提示
        if (isError) {
            console.error(message);
            logDebug('错误: ' + message);
        } else {
            console.log(message);
            logDebug('信息: ' + message);
        }
    }

    // 调试日志函数
    function logDebug(message) {
        const timestamp = new Date().toLocaleTimeString();
        const logEntry = `[${timestamp}] ${message}`;
        debugInfo.innerHTML += logEntry + '\n';
        debugInfo.scrollTop = debugInfo.scrollHeight;
    }

    // 切换调试信息显示
    toggleDebugBtn.addEventListener('click', () => {
        debugInfo.classList.toggle('debug-show');
        toggleDebugBtn.textContent = debugInfo.classList.contains('debug-show') 
            ? '隐藏调试信息' 
            : '显示调试信息';
    });

    // 辅助函数：带超时的fetch
    async function fetchWithTimeout(url, options = {}, timeout = 10000) {
        logDebug(`请求URL: ${url} (超时: ${timeout}ms)`);
        
        const controller = new AbortController();
        const id = setTimeout(() => controller.abort(), timeout);
        
        const defaultOptions = {
            signal: controller.signal,
            credentials: 'include', // 发送cookie
            mode: 'cors', // 启用CORS
            headers: {
                'Accept': 'application/json',
                'Content-Type': 'application/json'
            }
        };
        
        // 合并选项
        const fetchOptions = { ...defaultOptions, ...options };
        if (options.headers) {
            fetchOptions.headers = { ...defaultOptions.headers, ...options.headers };
        }
        
        logDebug(`请求选项: ${JSON.stringify(fetchOptions, (key, value) => 
            key === 'signal' ? '[AbortSignal]' : value)}`);
        
        try {
            const response = await fetch(url, fetchOptions);
            clearTimeout(id);
            
            logDebug(`响应状态: ${response.status} ${response.statusText}`);
            
            // 尝试获取响应头
            const headers = {};
            response.headers.forEach((value, key) => {
                headers[key] = value;
            });
            logDebug(`响应头: ${JSON.stringify(headers)}`);
            
            return response;
        } catch (error) {
            clearTimeout(id);
            if (error.name === 'AbortError') {
                logDebug(`请求超时 (${timeout}ms)`);
                throw new Error(`请求超时，请检查网络连接`);
            }
            logDebug(`请求错误: ${error.message}`);
            throw error;
        }
    }

    // 添加调试信息: API端点
    logDebug('页面加载完成');
    logDebug(`当前API基本URL: ${window.location.origin}`);
    logDebug(`当前页面URL: ${window.location.href}`);

    // 获取并显示所有权限组
    async function fetchGroups() {
        try {
            logDebug('开始获取权限组列表...');
            const url = '/api/groups';
            
            const response = await fetchWithTimeout(url);
            
            if (!response.ok) {
                const errorText = await response.text();
                console.error('获取权限组列表失败:', response.status, errorText);
                logDebug(`获取权限组列表失败: ${response.status} - ${errorText || response.statusText}`);
                showMessage(`获取权限组失败: HTTP ${response.status} - ${errorText || response.statusText}`, true);
                return;
            }
            
            const data = await response.json();
            logDebug(`获取到的权限组数据: ${JSON.stringify(data)}`);
            
            groupList.innerHTML = '';
            if (data.groups && data.groups.length > 0) {
                data.groups.forEach(group => {
                    const li = document.createElement('li');
                    li.textContent = group;
                    li.dataset.groupName = group;
                    li.addEventListener('click', () => selectGroup(group));
                    groupList.appendChild(li);
                });
                logDebug(`成功加载 ${data.groups.length} 个权限组`);
            } else {
                logDebug('没有找到任何权限组');
                groupList.innerHTML = '<li>没有权限组。</li>';
            }
        } catch (error) {
            console.error('获取权限组时出现异常:', error);
            logDebug(`获取权限组时出现异常: ${error.message}`);
            showMessage('获取权限组失败: ' + error.message, true);
        }
    }

    // 选择组并显示详情
    async function selectGroup(groupName) {
        currentGroup = groupName;
        groupDetailsSection.style.display = 'block';
        detailGroupName.textContent = groupName;

        try {
            const response = await fetchWithTimeout(`/api/groups/${groupName}`);
            const data = await response.json();
            if (response.ok) {
                detailGroupId.textContent = data.id;
                detailGroupDescription.textContent = data.description || '无';
                detailGroupPriority.textContent = data.priority;
                updateDescriptionInput.value = data.description || '';
                updatePriorityInput.value = data.priority;
                fetchGroupPermissions(groupName);
                fetchParentGroups(groupName);
                fetchPlayersInGroup(groupName);
            } else {
                showMessage(`获取组 ${groupName} 详情失败: ${data.error || response.statusText}`, true);
                groupDetailsSection.style.display = 'none';
            }
        } catch (error) {
            showMessage(`获取组 ${groupName} 详情失败: ${error.message}`, true);
            groupDetailsSection.style.display = 'none';
        }
    }

    // 获取组的权限
    async function fetchGroupPermissions(groupName) {
        try {
            const directResponse = await fetchWithTimeout(`/api/groups/${groupName}/permissions/direct`);
            const directData = await directResponse.json();
            directPermissionsList.innerHTML = '';
            if (directData.permissions && directData.permissions.length > 0) {
                directData.permissions.forEach(perm => {
                    const li = document.createElement('li');
                    li.textContent = perm;
                    directPermissionsList.appendChild(li);
                });
            } else {
                directPermissionsList.innerHTML = '<li>无直接权限。</li>';
            }

            const effectiveResponse = await fetchWithTimeout(`/api/groups/${groupName}/permissions/effective`);
            const effectiveData = await effectiveResponse.json();
            effectivePermissionsList.innerHTML = '';
            if (effectiveData.permissions && effectiveData.permissions.length > 0) {
                effectiveData.permissions.forEach(perm => {
                    const li = document.createElement('li');
                    li.textContent = perm;
                    effectivePermissionsList.appendChild(li);
                });
            } else {
                effectivePermissionsList.innerHTML = '<li>无生效权限。</li>';
            }

        } catch (error) {
            showMessage(`获取组 ${groupName} 权限失败: ${error.message}`, true);
        }
    }

    // 获取组的父组
    async function fetchParentGroups(groupName) {
        try {
            const response = await fetchWithTimeout(`/api/groups/${groupName}/parents`);
            const data = await response.json();
            parentGroupsList.innerHTML = '';
            if (data.parents && data.parents.length > 0) {
                data.parents.forEach(parent => {
                    const li = document.createElement('li');
                    li.textContent = parent;
                    parentGroupsList.appendChild(li);
                });
            } else {
                parentGroupsList.innerHTML = '<li>无父组。</li>';
            }
        } catch (error) {
            showMessage(`获取组 ${groupName} 父组失败: ${error.message}`, true);
        }
    }

    // 获取组中的玩家
    async function fetchPlayersInGroup(groupName) {
        try {
            const response = await fetchWithTimeout(`/api/groups/${groupName}/players`);
            const data = await response.json();
            playersInGroupList.innerHTML = '';
            if (data.players && data.players.length > 0) {
                data.players.forEach(player => {
                    const li = document.createElement('li');
                    li.textContent = player;
                    playersInGroupList.appendChild(li);
                });
            } else {
                playersInGroupList.innerHTML = '<li>无玩家。</li>';
            }
        } catch (error) {
            showMessage(`获取组 ${groupName} 玩家失败: ${error.message}`, true);
        }
    }

    // 初始化 - 打开调试信息并进行网络测试
    debugInfo.classList.add('debug-show');
    toggleDebugBtn.textContent = '隐藏调试信息';
    
    // 测试服务器连接
    async function testServerConnection() {
        logDebug('正在测试服务器连接...');
        try {
            const response = await fetchWithTimeout('/api/groups', {}, 5000);
            logDebug(`服务器连接测试成功: ${response.status}`);
            logDebug('正在获取权限组...');
            fetchGroups();
        } catch (error) {
            logDebug(`服务器连接测试失败: ${error.message}`);
            showMessage(`无法连接到服务器: ${error.message}。请检查服务器是否运行，或尝试刷新页面。`, true);
        }
    }
    
    // 运行测试
    testServerConnection();
    
    // 创建组按钮添加加载状态
    createGroupBtn.addEventListener('click', async () => {
        const originalText = createGroupBtn.textContent;
        createGroupBtn.textContent = '创建中...';
        createGroupBtn.disabled = true;
        
        const name = newGroupNameInput.value.trim();
        const description = newGroupDescriptionInput.value.trim();
        if (!name) {
            showMessage('组名称不能为空！', true);
            createGroupBtn.textContent = originalText;
            createGroupBtn.disabled = false;
            return;
        }
        
        try {
            logDebug(`开始创建权限组: ${name}`);
            const response = await fetchWithTimeout('/api/groups', {
                method: 'POST',
                body: JSON.stringify({ name, description })
            });
            
            const data = await response.json();
            if (response.ok) {
                logDebug(`成功创建权限组: ${name}`);
                showMessage(data.message);
                newGroupNameInput.value = '';
                newGroupDescriptionInput.value = '';
                fetchGroups();
            } else {
                logDebug(`创建权限组失败: ${data.error || response.statusText}`);
                showMessage(`创建组失败: ${data.error || response.statusText}`, true);
            }
        } catch (error) {
            logDebug(`创建权限组异常: ${error.message}`);
            showMessage('创建组失败: ' + error.message, true);
        } finally {
            createGroupBtn.textContent = originalText;
            createGroupBtn.disabled = false;
        }
    });

    // 事件监听器：更新描述
    updateDescriptionBtn.addEventListener('click', async () => {
        if (!currentGroup) return;
        const newDescription = updateDescriptionInput.value.trim();
        try {
            const response = await fetchWithTimeout(`/api/groups/${currentGroup}/description`, {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ description: newDescription })
            });
            const data = await response.json();
            if (response.ok) {
                showMessage(data.message);
                selectGroup(currentGroup); // 刷新详情
            } else {
                showMessage(`更新描述失败: ${data.error || response.statusText}`, true);
            }
        } catch (error) {
            showMessage('更新描述失败: ' + error.message, true);
        }
    });

    // 事件监听器：设置优先级
    updatePriorityBtn.addEventListener('click', async () => {
        if (!currentGroup) return;
        const priority = parseInt(updatePriorityInput.value.trim());
        if (isNaN(priority)) {
            showMessage('优先级必须是数字！', true);
            return;
        }
        try {
            const response = await fetchWithTimeout(`/api/groups/${currentGroup}/priority`, {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ priority })
            });
            const data = await response.json();
            if (response.ok) {
                showMessage(data.message);
                selectGroup(currentGroup); // 刷新详情
            } else {
                showMessage(`设置优先级失败: ${data.error || response.statusText}`, true);
            }
        } catch (error) {
            showMessage('设置优先级失败: ' + error.message, true);
        }
    });

    // 事件监听器：添加权限
    addPermissionBtn.addEventListener('click', async () => {
        if (!currentGroup) return;
        const permission = addPermissionInput.value.trim();
        if (!permission) {
            showMessage('权限规则不能为空！', true);
            return;
        }
        try {
            const response = await fetch(`/api/groups/${currentGroup}/permissions`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ permission })
            });
            const data = await response.json();
            if (response.ok) {
                showMessage(data.message);
                addPermissionInput.value = '';
                fetchGroupPermissions(currentGroup); // 刷新权限列表
            } else {
                showMessage(`添加权限失败: ${data.error || response.statusText}`, true);
            }
        } catch (error) {
            showMessage('添加权限失败: ' + error.message, true);
        }
    });

    // 事件监听器：移除权限
    removePermissionBtn.addEventListener('click', async () => {
        if (!currentGroup) return;
        const permission = removePermissionInput.value.trim();
        if (!permission) {
            showMessage('权限规则不能为空！', true);
            return;
        }
        try {
            const response = await fetch(`/api/groups/${currentGroup}/permissions`, {
                method: 'DELETE',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ permission })
            });
            const data = await response.json();
            if (response.ok) {
                showMessage(data.message);
                removePermissionInput.value = '';
                fetchGroupPermissions(currentGroup); // 刷新权限列表
            } else {
                showMessage(`移除权限失败: ${data.error || response.statusText}`, true);
            }
        } catch (error) {
            showMessage('移除权限失败: ' + error.message, true);
        }
    });

    // 事件监听器：添加父组
    addParentBtn.addEventListener('click', async () => {
        if (!currentGroup) return;
        const parentGroup = addParentInput.value.trim();
        if (!parentGroup) {
            showMessage('父组名称不能为空！', true);
            return;
        }
        try {
            const response = await fetch(`/api/groups/${currentGroup}/parents`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ parentGroup })
            });
            const data = await response.json();
            if (response.ok) {
                showMessage(data.message);
                addParentInput.value = '';
                fetchParentGroups(currentGroup); // 刷新父组列表
            } else {
                showMessage(`添加父组失败: ${data.error || response.statusText}`, true);
            }
        } catch (error) {
            showMessage('添加父组失败: ' + error.message, true);
        }
    });

    // 事件监听器：移除父组
    removeParentBtn.addEventListener('click', async () => {
        if (!currentGroup) return;
        const parentGroup = removeParentInput.value.trim();
        if (!parentGroup) {
            showMessage('父组名称不能为空！', true);
            return;
        }
        try {
            const response = await fetch(`/api/groups/${currentGroup}/parents`, {
                method: 'DELETE',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ parentGroup })
            });
            const data = await response.json();
            if (response.ok) {
                showMessage(data.message);
                removeParentInput.value = '';
                fetchParentGroups(currentGroup); // 刷新父组列表
            } else {
                showMessage(`移除父组失败: ${data.error || response.statusText}`, true);
            }
        } catch (error) {
            showMessage('移除父组失败: ' + error.message, true);
        }
    });

    // 事件监听器：添加玩家到组
    addPlayerBtn.addEventListener('click', async () => {
        if (!currentGroup) return;
        const playerUuid = addPlayerInput.value.trim();
        if (!playerUuid) {
            showMessage('玩家UUID不能为空！', true);
            return;
        }
        try {
            const response = await fetch(`/api/groups/${currentGroup}/players`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ playerUuid })
            });
            const data = await response.json();
            if (response.ok) {
                showMessage(data.message);
                addPlayerInput.value = '';
                fetchPlayersInGroup(currentGroup); // 刷新玩家列表
            } else {
                showMessage(`添加玩家失败: ${data.error || response.statusText}`, true);
            }
        } catch (error) {
            showMessage('添加玩家失败: ' + error.message, true);
        }
    });

    // 事件监听器：从组移除玩家
    removePlayerBtn.addEventListener('click', async () => {
        if (!currentGroup) return;
        const playerUuid = removePlayerInput.value.trim();
        if (!playerUuid) {
            showMessage('玩家UUID不能为空！', true);
            return;
        }
        try {
            const response = await fetch(`/api/groups/${currentGroup}/players`, {
                method: 'DELETE',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ playerUuid })
            });
            const data = await response.json();
            if (response.ok) {
                showMessage(data.message);
                removePlayerInput.value = '';
                fetchPlayersInGroup(currentGroup); // 刷新玩家列表
            } else {
                showMessage(`移除玩家失败: ${data.error || response.statusText}`, true);
            }
        } catch (error) {
            showMessage('移除玩家失败: ' + error.message, true);
        }
    });

    // 事件监听器：删除组
    deleteGroupBtn.addEventListener('click', async () => {
        if (!currentGroup) return;
        if (!confirm(`确定要删除组 "${currentGroup}" 吗？这将是不可逆的！`)) {
            return;
        }
        try {
            const response = await fetch(`/api/groups/${currentGroup}`, {
                method: 'DELETE'
            });
            const data = await response.json();
            if (response.ok) {
                showMessage(data.message);
                currentGroup = null;
                groupDetailsSection.style.display = 'none'; // 隐藏详情
                fetchGroups(); // 刷新组列表
            } else {
                showMessage(`删除组失败: ${data.error || response.statusText}`, true);
            }
        } catch (error) {
            showMessage('删除组失败: ' + error.message, true);
        }
    });

    // 初始加载组
    fetchGroups();
});
