import { Card, Dropdown, Tag, Tooltip, Typography } from 'antd';
import type { MenuProps } from 'antd';
import type { ArtifactRecord } from '../types';

const { Paragraph, Text } = Typography;

const TYPE_COLORS: Record<string, string> = {
  yaml: 'green',
  gdml: 'blue',
  so: 'purple',
  mac: 'orange',
  bundle: 'magenta',
  other: 'default',
};

function formatSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

export default function ArtifactCard({
  artifact,
  onClick,
  menu,
  onMenuClick,
  draggable,
  onDragStart,
}: {
  artifact: ArtifactRecord;
  onClick: () => void;
  menu?: MenuProps['items'];
  onMenuClick?: (key: string) => void;
  draggable?: boolean;
  onDragStart?: (e: React.DragEvent<HTMLDivElement>) => void;
}) {
  const { meta } = artifact;
  const author = meta.author?.alias || meta.author?.name || '';
  const institute = meta.affiliation?.institute || '';

  const card = (
    <Card
      hoverable
      onClick={onClick}
      draggable={draggable}
      onDragStart={onDragStart}
      size="small"
      style={{ height: '100%', width: '100%', display: 'flex', flexDirection: 'column' }}
      styles={{ body: { flex: 1, display: 'flex', flexDirection: 'column', gap: 8 } }}
    >
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, width: '100%', minWidth: 0 }}>
        <Tag color={TYPE_COLORS[artifact.type] ?? 'default'} style={{ marginInlineEnd: 0, flexShrink: 0 }}>
          {artifact.type}
        </Tag>
        <Tooltip title={artifact.name}>
          <Text strong ellipsis style={{ flex: 1, minWidth: 0 }}>{artifact.name}</Text>
        </Tooltip>
      </div>

      {meta.description && (
        <Paragraph type="secondary" ellipsis={{ rows: 2 }} style={{ fontSize: 13, marginBottom: 0 }}>
          {meta.description}
        </Paragraph>
      )}

      {meta.tags && meta.tags.length > 0 && (
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 4 }}>
          {meta.tags.map((t) => <Tag key={t} style={{ marginInlineEnd: 0 }}>{t}</Tag>)}
        </div>
      )}

      <div style={{ marginTop: 'auto' }}>
        {(author || institute) && (
          <Text type="secondary" ellipsis style={{ fontSize: 12, display: 'block' }}>
            {[author, institute].filter(Boolean).join(' · ')}
          </Text>
        )}
        <Tooltip title={`${artifact.sha256} · ${formatSize(artifact.size)}`}>
          <Text type="secondary" ellipsis style={{ fontSize: 11, fontFamily: 'monospace', display: 'block' }}>
            {artifact.sha256.slice(0, 12)}… · {formatSize(artifact.size)}
          </Text>
        </Tooltip>
      </div>
    </Card>
  );

  if (!menu) return card;
  return (
    <Dropdown menu={{ items: menu, onClick: ({ key }) => onMenuClick?.(key) }} trigger={['contextMenu']}>
      {card}
    </Dropdown>
  );
}
