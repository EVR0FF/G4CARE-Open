import { useCallback, useEffect, useState } from 'react';
import { Breadcrumb, Button, Col, Dropdown, Empty, Input, Layout, Menu, Modal, Popconfirm, Row, Segmented, Select, Space, Tag, Tree, Typography, message } from 'antd';
import type { MenuProps } from 'antd';
import { AppstoreOutlined, BarsOutlined, CopyOutlined, DeleteOutlined, DownloadOutlined, EditOutlined, FolderOpenOutlined, FolderOutlined, HomeOutlined, LogoutOutlined, ScissorOutlined, ShareAltOutlined, SnippetsOutlined, UploadOutlined } from '@ant-design/icons';
import { listArtifacts, listTrash, restoreArtifact, deleteArtifactForever, listFolders, createFolder, moveArtifact, renameFolder, deleteFolder, createShare, deleteArtifact, downloadUrl, updateArtifact, copyArtifact, moveFolder, copyFolder } from './api';
import type { ArtifactRecord, FolderRecord, SearchFilter } from './types';
import ArtifactCard from './components/ArtifactCard';
import ArtifactDetail from './components/ArtifactDetail';
import UploadForm from './components/UploadForm';
import AuthPage from './components/AuthPage';
import ShareView from './components/ShareView';
import ArtifactList from './components/ArtifactList';

const { Header, Content, Sider } = Layout;
const { Text } = Typography;

const TYPE_OPTIONS = ['', 'yaml', 'gdml', 'so', 'mac', 'bundle', 'other'];

function isTokenValid(token: string): boolean {
  try {
    const payload = token.split('.')[1];
    if (!payload) return false;
    const b64 = payload.replace(/-/g, '+').replace(/_/g, '/');
    const json = JSON.parse(atob(b64 + '='.repeat((4 - (b64.length % 4)) % 4)));
    return typeof json.exp === 'number' && json.exp * 1000 > Date.now();
  } catch {
    return false;
  }
}

export default function App() {
  const [artifacts, setArtifacts] = useState<ArtifactRecord[]>([]);
  const [filter, setFilter] = useState<SearchFilter>({});
  const [selected, setSelected] = useState<ArtifactRecord | null>(null);
  const [error, setError] = useState('');
  const [sortKey, setSortKey] = useState<'createdAt' | 'name' | 'type' | 'size'>('createdAt');
  const [sortDir, setSortDir] = useState<'asc' | 'desc'>('desc');
  const [uploadOpen, setUploadOpen] = useState(false);
  const [viewTrash, setViewTrash] = useState(false);
  const [trash, setTrash] = useState<ArtifactRecord[]>([]);
  const [messageApi, contextHolder] = message.useMessage();
  const [shareToken] = useState(() => new URLSearchParams(window.location.search).get('share') ?? '');
  const [folders, setFolders] = useState<FolderRecord[]>([]);
  const [newFolderOpen, setNewFolderOpen] = useState(false);
  const [newFolderName, setNewFolderName] = useState('');
  const [currentFolderId, setCurrentFolderId] = useState<string>('__root__');
  const [viewMode, setViewMode] = useState<'cards' | 'list'>('cards');
  const [moveTarget, setMoveTarget] = useState<ArtifactRecord | null>(null);
  const [renameTarget, setRenameTarget] = useState<{ type: 'artifact' | 'folder'; id: string; name: string } | null>(null);
  const [renameValue, setRenameValue] = useState('');
  const [shareOpen, setShareOpen] = useState(false);
  const [shareUrl, setShareUrl] = useState('');
  const [clipboard, setClipboard] = useState<{ mode: 'copy' | 'cut'; items: { type: 'artifact' | 'folder'; id: string }[] } | null>(null);
  const [ctx, setCtx] = useState<{ x: number; y: number; items: MenuProps['items']; onClick: (k: string) => void } | null>(null);
  const [folderMoveTarget, setFolderMoveTarget] = useState<FolderRecord | null>(null);
  const [token, setToken] = useState<string>(() => {
    const t = localStorage.getItem('g4care_token') ?? '';
    if (t && isTokenValid(t)) return t;
    localStorage.removeItem('g4care_token');
    localStorage.removeItem('g4care_username');
    return '';
  });
  const [username, setUsername] = useState<string>(() => localStorage.getItem('g4care_username') ?? '');

  const handleLogin = (t: string, u: string) => {
    setToken(t);
    setUsername(u);
    localStorage.setItem('g4care_token', t);
    localStorage.setItem('g4care_username', u);
  };

  const handleLogout = () => {
    setToken('');
    setUsername('');
    localStorage.removeItem('g4care_token');
    localStorage.removeItem('g4care_username');
  };

  const refresh = useCallback(async () => {
    try {
      const f: SearchFilter = { ...filter, folderId: currentFolderId === '__root__' ? '__root__' : currentFolderId };
      setArtifacts(await listArtifacts(f, token));
      setError('');
    } catch (e) {
      setError((e as Error).message);
    }
  }, [filter, token, currentFolderId]);

  useEffect(() => {
    refresh();
  }, [refresh]);

  const loadTrash = useCallback(async () => {
    try {
      setTrash(await listTrash(token));
      setError('');
    } catch (e) {
      setError((e as Error).message);
    }
  }, [token]);

  useEffect(() => {
    if (viewTrash) loadTrash();
  }, [viewTrash, loadTrash]);

  const doRestore = async (id: string) => {
    try {
      await restoreArtifact(id, token);
      messageApi.success('Restored');
      loadTrash();
      refresh();
    } catch (e) { messageApi.error((e as Error).message); }
  };

  const doPurge = async (id: string) => {
    try {
      await deleteArtifactForever(id, token);
      messageApi.success('Deleted forever');
      loadTrash();
    } catch (e) { messageApi.error((e as Error).message); }
  };

  const loadFolders = useCallback(async () => {
    try { setFolders(await listFolders(token)); } catch { /* ignore */ }
  }, [token]);

  useEffect(() => { loadFolders(); }, [loadFolders]);

  const doCreateFolder = async () => {
    if (!newFolderName.trim()) return;
    try {
      await createFolder(newFolderName.trim(), token, currentFolderId === '__root__' ? undefined : currentFolderId);
      messageApi.success('Folder created');
      setNewFolderName('');
      setNewFolderOpen(false);
      loadFolders();
    } catch (e) { messageApi.error((e as Error).message); }
  };

  const set = (key: keyof SearchFilter) => (e: React.ChangeEvent<HTMLInputElement | HTMLSelectElement>) => {
    setFilter((prev) => ({ ...prev, [key]: e.target.value || undefined }));
  };

  const doMove = async (folderId: string) => {
    if (!moveTarget) return;
    try {
      await moveArtifact(moveTarget.id, folderId || null, token);
      messageApi.success('Moved');
      setMoveTarget(null);
      refresh();
    } catch (e) { messageApi.error((e as Error).message); }
  };

  const openRename = (type: 'artifact' | 'folder', id: string, name: string) => {
    setRenameTarget({ type, id, name });
    setRenameValue(name);
  };

  const doRename = async () => {
    if (!renameTarget || !renameValue.trim()) return;
    try {
      if (renameTarget.type === 'artifact') {
        await updateArtifact(renameTarget.id, { name: renameValue.trim() }, token);
      } else {
        await renameFolder(renameTarget.id, renameValue.trim(), token);
      }
      messageApi.success('Renamed');
      setRenameTarget(null);
      refresh();
      loadFolders();
    } catch (e) { messageApi.error((e as Error).message); }
  };

  const doShareArtifact = async (a: ArtifactRecord) => {
    try {
      const res = await createShare({ artifactId: a.id }, token);
      setShareUrl(`${window.location.origin}/?share=${res.token}`);
      setShareOpen(true);
    } catch (e) { messageApi.error((e as Error).message); }
  };

  const doShareFolder = async (f: FolderRecord) => {
    try {
      const res = await createShare({ folderId: f.id }, token);
      setShareUrl(`${window.location.origin}/?share=${res.token}`);
      setShareOpen(true);
    } catch (e) { messageApi.error((e as Error).message); }
  };

  const doDeleteById = async (id: string) => {
    try {
      await deleteArtifact(id, token);
      messageApi.success('Moved to trash');
      refresh();
    } catch (e) { messageApi.error((e as Error).message); }
  };

  const doDeleteArtifact = (a: ArtifactRecord) => doDeleteById(a.id);

  const doDeleteFolder = async (id: string) => {
    try {
      await deleteFolder(id, token);
      messageApi.success('Folder deleted');
      loadFolders();
      refresh();
    } catch (e) { messageApi.error((e as Error).message); }
  };

  const doMoveById = async (id: string, folderId: string | null) => {
    try {
      await moveArtifact(id, folderId, token);
      messageApi.success('Moved');
      refresh();
    } catch (e) { messageApi.error((e as Error).message); }
  };

  const doCopy = (a: ArtifactRecord) => {
    setClipboard({ mode: 'copy', items: [{ type: 'artifact', id: a.id }] });
    messageApi.info('Copied');
  };

  const doCut = (a: ArtifactRecord) => {
    setClipboard({ mode: 'cut', items: [{ type: 'artifact', id: a.id }] });
    messageApi.info('Cut');
  };

  const doCopyFolder = (f: FolderRecord) => {
    setClipboard({ mode: 'copy', items: [{ type: 'folder', id: f.id }] });
    messageApi.info('Copied folder');
  };

  const doCutFolder = (f: FolderRecord) => {
    setClipboard({ mode: 'cut', items: [{ type: 'folder', id: f.id }] });
    messageApi.info('Cut folder');
  };

  const doPaste = async (folderId: string | null) => {
    if (!clipboard) return;
    const { mode, items } = clipboard;
    try {
      for (const it of items) {
        if (mode === 'copy') {
          if (it.type === 'artifact') await copyArtifact(it.id, folderId, token);
          else await copyFolder(it.id, folderId, token);
        } else {
          if (it.type === 'artifact') await moveArtifact(it.id, folderId, token);
          else await moveFolder(it.id, folderId, token);
        }
      }
      setClipboard(null);
      messageApi.success(mode === 'copy' ? 'Copied' : 'Moved');
      refresh();
      loadFolders();
    } catch (e) { messageApi.error((e as Error).message); }
  };

  const doMoveFolder = async (parentId: string) => {
    if (!folderMoveTarget) return;
    try {
      await moveFolder(folderMoveTarget.id, parentId || null, token);
      messageApi.success('Moved');
      setFolderMoveTarget(null);
      loadFolders();
    } catch (e) { messageApi.error((e as Error).message); }
  };

  const openCtx = (e: React.MouseEvent, items: MenuProps['items'], onClick: (k: string) => void) => {
    e.preventDefault();
    setCtx({ x: e.clientX, y: e.clientY, items, onClick });
  };

  const artifactMenu = (a: ArtifactRecord): MenuProps['items'] => [
    { key: 'open', label: 'Open', icon: <FolderOpenOutlined /> },
    { key: 'download', label: 'Download', icon: <DownloadOutlined /> },
    { type: 'divider' },
    { key: 'rename', label: 'Rename', icon: <EditOutlined /> },
    { key: 'move', label: 'Move to…', icon: <FolderOutlined /> },
    { key: 'copy', label: 'Copy', icon: <CopyOutlined /> },
    { key: 'cut', label: 'Cut', icon: <ScissorOutlined /> },
    { key: 'share', label: 'Share', icon: <ShareAltOutlined /> },
    { type: 'divider' },
    { key: 'delete', label: 'Delete', icon: <DeleteOutlined />, danger: true },
  ];

  const handleArtifactMenu = (a: ArtifactRecord, key: string) => {
    switch (key) {
      case 'open': setSelected(a); break;
      case 'download': window.open(downloadUrl(a.id, token), '_blank'); break;
      case 'rename': openRename('artifact', a.id, a.name); break;
      case 'move': setMoveTarget(a); break;
      case 'copy': doCopy(a); break;
      case 'cut': doCut(a); break;
      case 'share': doShareArtifact(a); break;
      case 'delete': doDeleteArtifact(a); break;
    }
  };

  const folderMenu = (f: FolderRecord): MenuProps['items'] => [
    { key: 'open', label: 'Open', icon: <FolderOpenOutlined /> },
    { key: 'rename', label: 'Rename', icon: <EditOutlined /> },
    { key: 'move', label: 'Move to…', icon: <FolderOutlined /> },
    { key: 'copy', label: 'Copy', icon: <CopyOutlined /> },
    { key: 'cut', label: 'Cut', icon: <ScissorOutlined /> },
    { key: 'share', label: 'Share', icon: <ShareAltOutlined /> },
    ...(clipboard ? [{ key: 'paste', label: 'Paste here', icon: <SnippetsOutlined /> }] : []),
    { type: 'divider' },
    { key: 'delete', label: 'Delete', icon: <DeleteOutlined />, danger: true },
  ];

  const handleFolderMenu = (f: FolderRecord, key: string) => {
    switch (key) {
      case 'open': setViewTrash(false); setCurrentFolderId(f.id); setSelected(null); break;
      case 'rename': openRename('folder', f.id, f.name); break;
      case 'move': setFolderMoveTarget(f); break;
      case 'copy': doCopyFolder(f); break;
      case 'cut': doCutFolder(f); break;
      case 'share': doShareFolder(f); break;
      case 'paste': doPaste(f.id); break;
      case 'delete': doDeleteFolder(f.id); break;
    }
  };

  const buildTree = (parentId?: string): any[] =>
    folders
      .filter((f) => (parentId === undefined ? !f.parentId : f.parentId === parentId))
      .map((f) => ({
        key: f.id,
        title: (
          <Dropdown menu={{ items: folderMenu(f), onClick: ({ key }) => handleFolderMenu(f, key) }} trigger={['contextMenu']}>
            <span
              onDragOver={(e) => { e.preventDefault(); e.dataTransfer.dropEffect = 'move'; }}
              onDrop={(e) => { e.preventDefault(); const id = e.dataTransfer.getData('text/plain'); if (id) doMoveById(id, f.id); }}
            >
              {f.name}
            </span>
          </Dropdown>
        ),
        icon: <FolderOutlined />,
        children: buildTree(f.id),
      }));

  const folderPath = (id: string): FolderRecord[] => {
    const path: FolderRecord[] = [];
    let cur = folders.find((f) => f.id === id);
    const seen = new Set<string>();
    while (cur && !seen.has(cur.id)) {
      seen.add(cur.id);
      path.unshift(cur);
      cur = folders.find((f) => f.id === cur!.parentId);
    }
    return path;
  };

  const treeData = [
    {
      key: '__root__',
      title: (
        <span
          onDragOver={(e) => { e.preventDefault(); e.dataTransfer.dropEffect = 'move'; }}
          onDrop={(e) => { e.preventDefault(); const id = e.dataTransfer.getData('text/plain'); if (id) doMoveById(id, null); }}
        >
          All files
        </span>
      ),
      icon: <HomeOutlined />,
    },
    ...buildTree(undefined),
    {
      key: '__trash__',
      title: (
        <span
          onDragOver={(e) => { e.preventDefault(); e.dataTransfer.dropEffect = 'move'; }}
          onDrop={(e) => { e.preventDefault(); const id = e.dataTransfer.getData('text/plain'); if (id) doDeleteById(id); }}
        >
          Trash
        </span>
      ),
      icon: <DeleteOutlined />,
    },
  ];

  const onTreeSelect = (keys: React.Key[]) => {
    const k = String(keys[0] ?? '');
    if (!k) return;
    if (k === '__trash__') { setViewTrash(true); return; }
    setViewTrash(false);
    setCurrentFolderId(k);
    setSelected(null);
  };

  if (shareToken) {
    return <ShareView token={shareToken} />;
  }

  if (!token) {
    return <AuthPage onLogin={handleLogin} />;
  }

  const sorted = [...artifacts].sort((a, b) => {
    let cmp = 0;
    if (sortKey === 'name') cmp = a.name.localeCompare(b.name);
    else if (sortKey === 'type') cmp = a.type.localeCompare(b.type);
    else if (sortKey === 'size') cmp = a.size - b.size;
    else cmp = (a.createdAt ?? '').localeCompare(b.createdAt ?? '');
    return sortDir === 'asc' ? cmp : -cmp;
  });

  return (
    <Layout style={{ minHeight: '100vh' }}>
      {contextHolder}
      <Header style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 16, background: '#fff', padding: '0 24px', boxShadow: '0 1px 4px rgba(0,0,0,.08)', height: 64, lineHeight: '64px' }}>
        <Text strong style={{ fontSize: 18, whiteSpace: 'nowrap' }}>G4CARE Cloud</Text>
        <Space size={12}>
          <Text type="secondary" style={{ fontSize: 13, whiteSpace: 'nowrap' }}>Signed in as {username}</Text>
          <Button type="primary" icon={<UploadOutlined />} onClick={() => setUploadOpen(true)}>Upload</Button>
          <Button icon={<LogoutOutlined />} onClick={handleLogout}>Log out</Button>
        </Space>
      </Header>

      <Layout>
        <Sider width={220} theme="light" style={{ borderRight: '1px solid #f0f0f0' }}>
          <div style={{ padding: '12px 12px 8px' }}>
            <Button block icon={<FolderOutlined />} onClick={() => setNewFolderOpen(true)}>New folder</Button>
          </div>
          <Tree
            treeData={treeData}
            selectedKeys={[viewTrash ? '__trash__' : currentFolderId]}
            onSelect={onTreeSelect}
            defaultExpandAll
          />
        </Sider>
        <Content style={{ padding: 24, width: '100%', minWidth: 0 }}>
          {!viewTrash && (
            <Breadcrumb
              style={{ marginBottom: 12 }}
              items={[
                { title: <a onClick={() => { setViewTrash(false); setCurrentFolderId('__root__'); }}>All files</a> },
                ...(currentFolderId === '__root__' ? [] : folderPath(currentFolderId).map((f) => ({ title: <a onClick={() => setCurrentFolderId(f.id)}>{f.name}</a> }))),
              ]}
            />
          )}
          <Space wrap style={{ marginBottom: 16 }}>
          <Input
            placeholder="Search name / description…"
            allowClear
            style={{ width: 240 }}
            value={filter.q ?? ''}
            onChange={set('q')}
          />
          <Input placeholder="Tag" style={{ width: 130 }} value={filter.tag ?? ''} onChange={set('tag')} />
          <Input placeholder="Author" style={{ width: 150 }} value={filter.author ?? ''} onChange={set('author')} />
          <Input placeholder="Institute" style={{ width: 150 }} value={filter.institute ?? ''} onChange={set('institute')} />
          <Select
            style={{ width: 140 }}
            value={filter.type ?? ''}
            onChange={(v: string) => setFilter((p) => ({ ...p, type: v || undefined }))}
            options={TYPE_OPTIONS.map((t) => ({ value: t, label: t || 'all types' }))}
          />
          <Select
            style={{ width: 160 }}
            value={`${sortKey}:${sortDir}`}
            onChange={(v: string) => {
              const [k, d] = v.split(':');
              setSortKey(k as 'createdAt' | 'name' | 'type' | 'size');
              setSortDir(d as 'asc' | 'desc');
            }}
            options={[
              { value: 'createdAt:desc', label: 'Newest first' },
              { value: 'createdAt:asc', label: 'Oldest first' },
              { value: 'name:asc', label: 'Name A→Z' },
              { value: 'name:desc', label: 'Name Z→A' },
              { value: 'size:desc', label: 'Largest first' },
              { value: 'size:asc', label: 'Smallest first' },
            ]}
          />
          <Button
            type={viewTrash ? 'primary' : 'default'}
            onClick={() => { setViewTrash(!viewTrash); setSelected(null); }}
          >
            {viewTrash ? 'Back to files' : 'Trash'}
          </Button>
          <Segmented
            value={viewMode}
            onChange={(v) => setViewMode(v as 'cards' | 'list')}
            options={[
              { value: 'cards', label: 'Cards', icon: <AppstoreOutlined /> },
              { value: 'list', label: 'List', icon: <BarsOutlined /> },
            ]}
          />
          {clipboard && (
            <Space size={4}>
              <Tag color="blue">{clipboard.mode === 'copy' ? 'Copied' : 'Cut'} ({clipboard.items.length})</Tag>
              <Button size="small" type="primary" onClick={() => doPaste(currentFolderId === '__root__' ? null : currentFolderId)}>Paste here</Button>
              <Button size="small" onClick={() => setClipboard(null)}>Clear</Button>
            </Space>
          )}
        </Space>

        {error && <div style={{ color: '#cf1322', marginBottom: 12 }}>{error}</div>}

        {viewTrash ? (
          trash.length === 0 ? (
            <Empty description="Trash is empty" style={{ padding: 40 }} />
          ) : (
            <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
              {trash.map((t) => (
                <div key={t.id} style={{ display: 'flex', alignItems: 'center', gap: 12, padding: '8px 12px', background: '#fff', borderRadius: 8, border: '1px solid #f0f0f0' }}>
                  <Tag>{t.type}</Tag>
                  <Text strong style={{ flex: 1, minWidth: 0 }} ellipsis>{t.name}</Text>
                  <Text type="secondary" style={{ fontSize: 12, whiteSpace: 'nowrap' }}>
                    {t.deletedAt ? t.deletedAt.slice(0, 10) : ''}
                  </Text>
                  <Button size="small" onClick={() => doRestore(t.id)}>Restore</Button>
                  <Popconfirm title="Delete forever?" onConfirm={() => doPurge(t.id)} okText="Delete" okButtonProps={{ danger: true }}>
                    <Button size="small" danger>Delete forever</Button>
                  </Popconfirm>
                </div>
              ))}
            </div>
          )
        ) : viewMode === 'list' ? (
          <ArtifactList artifacts={sorted} onClick={(a) => setSelected(a)} menu={artifactMenu} onMenuClick={handleArtifactMenu} onContextMenu={(a, e) => openCtx(e, artifactMenu(a), (k) => handleArtifactMenu(a, k))} />
        ) : (
          <Row gutter={[16, 16]}>
            {artifacts.length === 0 && !error && (
              <Col span={24}>
                <Text type="secondary" style={{ display: 'block', textAlign: 'center', padding: 40 }}>
                  No artifacts found.
                </Text>
              </Col>
            )}
            {sorted.map((a) => (
              <Col key={a.id} xs={24} sm={12} md={8} lg={6} style={{ display: 'flex' }}>
                <ArtifactCard artifact={a} onClick={() => setSelected(a)} menu={artifactMenu(a)} onMenuClick={(k) => handleArtifactMenu(a, k)} draggable onDragStart={(e) => e.dataTransfer.setData('text/plain', a.id)} />
              </Col>
            ))}
          </Row>
        )}
        </Content>
      </Layout>

      <Modal
        title="Upload files"
        open={uploadOpen}
        onCancel={() => setUploadOpen(false)}
        footer={null}
        width={520}
      >
        <UploadForm onUploaded={() => { refresh(); setUploadOpen(false); }} token={token} />
      </Modal>

      <Modal
        title="New folder"
        open={newFolderOpen}
        onCancel={() => setNewFolderOpen(false)}
        onOk={doCreateFolder}
        okText="Create"
      >
        <Input
          placeholder="Folder name"
          value={newFolderName}
          onChange={(e) => setNewFolderName(e.target.value)}
          onPressEnter={doCreateFolder}
        />
      </Modal>

      <Modal
        title={moveTarget ? `Move "${moveTarget.name}" to…` : 'Move'}
        open={!!moveTarget}
        onCancel={() => setMoveTarget(null)}
        footer={null}
      >
        <Select
          style={{ width: '100%' }}
          placeholder="Choose folder"
          onChange={(v: string) => doMove(v)}
          options={[
            { value: '', label: 'No folder (root)' },
            ...folders.map((f) => ({ value: f.id, label: f.name })),
          ]}
        />
      </Modal>

      <Modal
        title={folderMoveTarget ? `Move folder "${folderMoveTarget.name}" to…` : 'Move folder'}
        open={!!folderMoveTarget}
        onCancel={() => setFolderMoveTarget(null)}
        footer={null}
      >
        <Select
          style={{ width: '100%' }}
          placeholder="Choose parent folder"
          onChange={(v: string) => doMoveFolder(v)}
          options={[
            { value: '', label: 'No folder (root)' },
            ...folders
              .filter((f) => f.id !== folderMoveTarget?.id)
              .map((f) => ({ value: f.id, label: f.name })),
          ]}
        />
      </Modal>

      <Modal
        title={renameTarget?.type === 'folder' ? 'Rename folder' : 'Rename file'}
        open={!!renameTarget}
        onCancel={() => setRenameTarget(null)}
        onOk={doRename}
        okText="Rename"
      >
        <Input value={renameValue} onChange={(e) => setRenameValue(e.target.value)} onPressEnter={doRename} />
      </Modal>

      <Modal title="Share link" open={shareOpen} onCancel={() => setShareOpen(false)} footer={null}>
        <Text type="secondary">Anyone with this link can view and download.</Text>
        <Input.TextArea value={shareUrl} readOnly autoSize style={{ marginTop: 8 }} />
        <Button
          style={{ marginTop: 8 }}
          onClick={() => { navigator.clipboard?.writeText(shareUrl); messageApi.success('Copied'); }}
        >
          Copy
        </Button>
      </Modal>

      {selected && (
        <ArtifactDetail
          artifact={selected}
          onClose={() => setSelected(null)}
          username={username}
          token={token}
          onChanged={refresh}
          onOpen={(a) => setSelected(a)}
        />
      )}

      {ctx && (
        <>
          <div style={{ position: 'fixed', inset: 0, zIndex: 1040 }} onClick={() => setCtx(null)} />
          <div style={{ position: 'fixed', left: ctx.x, top: ctx.y, zIndex: 1050, minWidth: 180 }}>
            <Menu
              items={ctx.items}
              onClick={({ key }) => { ctx.onClick(String(key)); setCtx(null); }}
              style={{ boxShadow: '0 6px 16px rgba(0,0,0,.12)', border: '1px solid #f0f0f0', borderRadius: 8 }}
            />
          </div>
        </>
      )}
    </Layout>
  );
}
