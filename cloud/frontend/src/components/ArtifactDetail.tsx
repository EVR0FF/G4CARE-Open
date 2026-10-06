import { useEffect, useState } from 'react';
import {
  Button, Descriptions, Drawer, Form, Input, Modal, Popconfirm, Select, Space, Tag, Typography, message,
} from 'antd';
import { DeleteOutlined, DownloadOutlined, EditOutlined, ShareAltOutlined } from '@ant-design/icons';
import type { ArtifactRecord } from '../types';
import { downloadUrl, previewUrl, deleteArtifact, updateArtifact, getVersions, createShare } from '../api';
import Viewer from './Viewer';

const { Text, Title } = Typography;

export default function ArtifactDetail({
  artifact,
  onClose,
  username,
  token,
  onChanged,
  onOpen,
}: {
  artifact: ArtifactRecord;
  onClose: () => void;
  username: string;
  token?: string;
  onChanged: () => void;
  onOpen?: (a: ArtifactRecord) => void;
}) {
  const { meta } = artifact;
  const isOwner = !!username && artifact.owner === username;
  const isText = artifact.type === 'yaml' || artifact.type === 'mac' || artifact.type === 'other';
  const [text, setText] = useState<string | null>(null);
  const [editOpen, setEditOpen] = useState(false);
  const [messageApi, contextHolder] = message.useMessage();
  const [form] = Form.useForm();
  const [versions, setVersions] = useState<ArtifactRecord[]>([]);
  const [shareOpen, setShareOpen] = useState(false);
  const [shareUrl, setShareUrl] = useState('');

  useEffect(() => {
    if (!isText) { setText(null); return; }
    let cancelled = false;
    fetch(downloadUrl(artifact.id, token))
      .then((r) => r.text())
      .then((t) => { if (!cancelled) setText(t); })
      .catch(() => { if (!cancelled) setText('(preview unavailable)'); });
    return () => { cancelled = true; };
  }, [artifact.id, isText, token]);

  useEffect(() => {
    let cancelled = false;
    getVersions(artifact.id, token)
      .then((v) => { if (!cancelled) setVersions(v); })
      .catch(() => {});
    return () => { cancelled = true; };
  }, [artifact.id, token]);

  const author =
    [meta.author?.name, meta.author?.alias ? `(${meta.author.alias})` : '']
      .filter(Boolean)
      .join(' ') || '—';

  const doDelete = async () => {
    try {
      await deleteArtifact(artifact.id, token);
      messageApi.success('Deleted');
      onChanged();
      onClose();
    } catch (e) { messageApi.error((e as Error).message); }
  };

  const doEdit = async (v: Record<string, unknown>) => {
    try {
      await updateArtifact(artifact.id, {
        name: v.name,
        description: v.description || undefined,
        version: v.version || undefined,
        doi: v.doi || undefined,
        visibility: v.visibility,
        authorName: v.authorName || undefined,
        authorAlias: v.authorAlias || undefined,
        created: v.created || undefined,
        institute: v.institute || undefined,
        group: v.group || undefined,
        tags: typeof v.tags === 'string'
          ? (v.tags as string).split(',').map((s) => s.trim()).filter(Boolean)
          : undefined,
      }, token);
      messageApi.success('Saved');
      setEditOpen(false);
      onChanged();
    } catch (e) { messageApi.error((e as Error).message); }
  };

  const openEdit = () => {
    form.setFieldsValue({
      name: artifact.name,
      description: meta.description,
      version: meta.version,
      doi: meta.doi,
      visibility: artifact.visibility ?? 'public',
      tags: (meta.tags ?? []).join(', '),
      authorName: meta.author?.name,
      authorAlias: meta.author?.alias,
      created: meta.created,
      institute: meta.affiliation?.institute,
      group: meta.affiliation?.group,
    });
    setEditOpen(true);
  };

  const doShare = async () => {
    try {
      const res = await createShare({ artifactId: artifact.id }, token);
      setShareUrl(`${window.location.origin}/?share=${res.token}`);
      setShareOpen(true);
    } catch (e) { messageApi.error((e as Error).message); }
  };

  return (
    <Drawer open width={520} onClose={onClose} title={artifact.name}>
      {contextHolder}
      <Descriptions column={1} size="small">
        <Descriptions.Item label="Type"><Tag>{artifact.type}</Tag></Descriptions.Item>
        <Descriptions.Item label="Owner">{artifact.owner || '—'}</Descriptions.Item>
        <Descriptions.Item label="Visibility">{artifact.visibility ?? 'public'}</Descriptions.Item>
        <Descriptions.Item label="Version">{artifact.version ?? 1}</Descriptions.Item>
        <Descriptions.Item label="DOI">{meta.doi || '—'}</Descriptions.Item>
        <Descriptions.Item label="Meta version">{meta.version || '—'}</Descriptions.Item>
        <Descriptions.Item label="Created">{meta.created || '—'}</Descriptions.Item>
        <Descriptions.Item label="Author">{author}</Descriptions.Item>
        <Descriptions.Item label="Institute">{meta.affiliation?.institute || '—'}</Descriptions.Item>
        <Descriptions.Item label="SHA-256">
          <Text style={{ fontFamily: 'monospace', fontSize: 12, wordBreak: 'break-all' }}>{artifact.sha256}</Text>
        </Descriptions.Item>
      </Descriptions>

      {versions.length > 1 && (
        <>
          <Title level={5}>Versions</Title>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
            {versions.map((v) => (
              <div key={v.id} style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                <Tag color={v.id === artifact.id ? 'blue' : 'default'}>v{v.version ?? 1}</Tag>
                <Text type="secondary" style={{ fontSize: 12 }}>
                  {v.createdAt ? v.createdAt.slice(0, 10) : '—'}
                </Text>
                {v.id !== artifact.id && onOpen && (
                  <Button size="small" type="link" style={{ padding: 0 }} onClick={() => onOpen(v)}>Open</Button>
                )}
              </div>
            ))}
          </div>
        </>
      )}

      {meta.description && <p>{meta.description}</p>}
      {meta.info_url && (
        <p><a href={meta.info_url} target="_blank" rel="noreferrer">More information</a></p>
      )}

      {artifact.files && artifact.files.length > 0 && (
        <>
          <Title level={5}>Files ({artifact.files.length})</Title>
          <ul>{artifact.files.map((f) => <li key={f.path}>{f.path}</li>)}</ul>
        </>
      )}

      {artifact.type === 'gdml' && (
        <>
          <Title level={5}>3D preview</Title>
          <Viewer url={previewUrl(artifact.id, token)} />
        </>
      )}

      {isText && (
        <>
          <Title level={5}>Preview</Title>
          <pre className="text-preview">{text ?? 'Loading…'}</pre>
        </>
      )}

      <Space style={{ marginTop: 16 }} wrap>
        <Button type="primary" icon={<DownloadOutlined />} href={downloadUrl(artifact.id, token)}>Download</Button>
        {isOwner && (
          <>
            <Button icon={<ShareAltOutlined />} onClick={doShare}>Share</Button>
            <Button icon={<EditOutlined />} onClick={openEdit}>Edit</Button>
            <Popconfirm title="Delete this artifact?" onConfirm={doDelete} okText="Delete" okButtonProps={{ danger: true }}>
              <Button danger icon={<DeleteOutlined />}>Delete</Button>
            </Popconfirm>
          </>
        )}
      </Space>

      <Modal open={editOpen} title="Edit artifact" onCancel={() => setEditOpen(false)} onOk={() => form.submit()} okText="Save">
        <Form form={form} layout="vertical" onFinish={doEdit}>
          <Form.Item name="name" label="Name" rules={[{ required: true, message: 'Name is required' }]}>
            <Input />
          </Form.Item>
          <Form.Item name="description" label="Description"><Input.TextArea rows={3} /></Form.Item>
          <Form.Item name="version" label="Meta version"><Input /></Form.Item>
          <Form.Item name="doi" label="DOI"><Input /></Form.Item>
          <Form.Item name="authorName" label="Author name"><Input /></Form.Item>
          <Form.Item name="authorAlias" label="Author alias"><Input /></Form.Item>
          <Form.Item name="created" label="Created"><Input placeholder="YYYY-MM-DD" /></Form.Item>
          <Form.Item name="institute" label="Institute"><Input /></Form.Item>
          <Form.Item name="group" label="Group"><Input /></Form.Item>
          <Form.Item name="tags" label="Tags (comma-separated)"><Input /></Form.Item>
          <Form.Item name="visibility" label="Visibility">
            <Select options={[{ value: 'public', label: 'Public' }, { value: 'private', label: 'Private' }]} />
          </Form.Item>
        </Form>
      </Modal>

      <Modal title="Share link" open={shareOpen} onCancel={() => setShareOpen(false)} footer={null}>
        <Text type="secondary">Anyone with this link can view and download this file.</Text>
        <Input.TextArea value={shareUrl} readOnly autoSize style={{ marginTop: 8 }} />
        <Button
          style={{ marginTop: 8 }}
          onClick={() => { navigator.clipboard?.writeText(shareUrl); messageApi.success('Copied'); }}
        >
          Copy
        </Button>
      </Modal>
    </Drawer>
  );
}
