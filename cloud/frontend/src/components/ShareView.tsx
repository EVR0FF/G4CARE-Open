import { useEffect, useState } from 'react';
import { Alert, Button, Card, Descriptions, Layout, List, Spin, Tag, Typography } from 'antd';
import { DownloadOutlined } from '@ant-design/icons';
import { getShare } from '../api';
import type { ArtifactRecord, FolderRecord } from '../types';

const { Text, Title } = Typography;

export default function ShareView({ token }: { token: string }) {
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const [artifact, setArtifact] = useState<ArtifactRecord | null>(null);
  const [folder, setFolder] = useState<FolderRecord | null>(null);
  const [artifacts, setArtifacts] = useState<ArtifactRecord[]>([]);
  const [download, setDownload] = useState('');

  useEffect(() => {
    getShare(token)
      .then((r) => {
        setArtifact(r.artifact ?? null);
        setFolder(r.folder ?? null);
        setArtifacts(r.artifacts ?? []);
        setDownload(r.download ?? '');
      })
      .catch((e) => setError((e as Error).message))
      .finally(() => setLoading(false));
  }, [token]);

  return (
    <Layout style={{ minHeight: '100vh', background: '#f4f5f7' }}>
      <div style={{ maxWidth: 720, margin: '0 auto', padding: 32, width: '100%' }}>
        <Card>
          <Title level={3}>G4CARE Cloud</Title>
          <Text type="secondary">Shared with you</Text>
          {loading && <div style={{ padding: 24, textAlign: 'center' }}><Spin /></div>}
          {error && <Alert type="error" message={error} showIcon style={{ marginTop: 12 }} />}
          {!loading && !error && artifact && (
            <div style={{ marginTop: 16 }}>
              <Descriptions column={1} size="small">
                <Descriptions.Item label="File">{artifact.name}</Descriptions.Item>
                <Descriptions.Item label="Type"><Tag>{artifact.type}</Tag></Descriptions.Item>
                <Descriptions.Item label="Author">{artifact.meta.author?.name || '—'}</Descriptions.Item>
                <Descriptions.Item label="Institute">{artifact.meta.affiliation?.institute || '—'}</Descriptions.Item>
              </Descriptions>
              {artifact.meta.description && <p>{artifact.meta.description}</p>}
              <Button type="primary" icon={<DownloadOutlined />} href={download} disabled={!download}>Download</Button>
            </div>
          )}
          {!loading && !error && folder && (
            <div style={{ marginTop: 16 }}>
              <Text strong>Folder: {folder.name}</Text>
              <List
                style={{ marginTop: 12 }}
                dataSource={artifacts}
                locale={{ emptyText: 'This folder is empty' }}
                renderItem={(a) => (
                  <List.Item>
                    <Tag>{a.type}</Tag>
                    <Text strong>{a.name}</Text>
                  </List.Item>
                )}
              />
            </div>
          )}
        </Card>
      </div>
    </Layout>
  );
}
