import { Button, Dropdown, Table, Tag, Typography } from 'antd';
import { MoreOutlined } from '@ant-design/icons';
import type { MenuProps } from 'antd';
import type { ColumnsType } from 'antd/es/table';
import type { ArtifactRecord } from '../types';

const { Text } = Typography;

function formatSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

export default function ArtifactList({ artifacts, onClick, menu, onMenuClick, onContextMenu }: {
  artifacts: ArtifactRecord[];
  onClick: (a: ArtifactRecord) => void;
  menu?: (a: ArtifactRecord) => MenuProps['items'];
  onMenuClick?: (a: ArtifactRecord, key: string) => void;
  onContextMenu?: (a: ArtifactRecord, e: React.MouseEvent) => void;
}) {
  const columns: ColumnsType<ArtifactRecord> = [
    { title: 'Name', dataIndex: 'name', key: 'name', render: (_: unknown, r: ArtifactRecord) => <Text strong>{r.name}</Text> },
    { title: 'Type', dataIndex: 'type', key: 'type', width: 90, render: (t: string) => <Tag>{t}</Tag> },
    { title: 'Size', dataIndex: 'size', key: 'size', width: 100, render: (s: number) => formatSize(s) },
    { title: 'Author', key: 'author', width: 160, render: (_: unknown, r: ArtifactRecord) => r.meta.author?.alias || r.meta.author?.name || '—' },
    { title: 'Created', dataIndex: 'createdAt', key: 'createdAt', width: 120, render: (d: string) => (d ? d.slice(0, 10) : '—') },
    ...(menu ? [{
      title: '',
      key: 'actions',
      width: 48,
      render: (_: unknown, a: ArtifactRecord) => (
        <Dropdown menu={{ items: menu(a), onClick: ({ key }) => onMenuClick?.(a, key) }} trigger={['click']}>
          <Button type="text" size="small" icon={<MoreOutlined />} onClick={(e) => e.stopPropagation()} />
        </Dropdown>
      ),
    }] : []),
  ];

  return (
    <Table
      rowKey="id"
      columns={columns}
      dataSource={artifacts}
      pagination={false}
      size="middle"
      onRow={(r) => ({
        onClick: () => onClick(r),
        onContextMenu: (e) => { e.preventDefault(); onContextMenu?.(r, e); },
        style: { cursor: 'pointer' },
      })}
    />
  );
}
