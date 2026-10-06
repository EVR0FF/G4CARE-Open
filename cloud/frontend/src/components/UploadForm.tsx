import { useState } from 'react';
import { Button, Input, message, Upload } from 'antd';
import { InboxOutlined, UploadOutlined } from '@ant-design/icons';
import type { UploadFile } from 'antd';
import { uploadArtifact } from '../api';

export default function UploadForm({ onUploaded, token }: { onUploaded: () => void; token?: string }) {
  const [fileList, setFileList] = useState<UploadFile[]>([]);
  const [meta, setMeta] = useState('');
  const [busy, setBusy] = useState(false);
  const [messageApi, contextHolder] = message.useMessage();

  const submit = async () => {
    const files = fileList.map((f) => f.originFileObj as File).filter(Boolean);
    if (files.length === 0) return;
    setBusy(true);
    try {
      // Separate `.meta.toml` sidecars from real artifacts: `<file>.meta.toml`
      // becomes the metadata for `<file>` and is never uploaded on its own.
      const sidecars = new Map<string, string>();
      const artifacts: File[] = [];
      for (const f of files) {
        if (f.name.endsWith('.meta.toml')) {
          sidecars.set(f.name.slice(0, -'.meta.toml'.length), await f.text());
        } else {
          artifacts.push(f);
        }
      }

      if (artifacts.length === 0) {
        messageApi.error('No files to upload (only .meta.toml sidecars were selected)');
        return;
      }

      for (const file of artifacts) {
        await uploadArtifact(file, sidecars.get(file.name) ?? meta, token);
      }

      const orphan = [...sidecars.keys()].filter((k) => !artifacts.some((a) => a.name === k));
      setFileList([]);
      setMeta('');
      const msg = `Uploaded ${artifacts.length} file${artifacts.length > 1 ? 's' : ''}` +
        (orphan.length > 0 ? ` · ignored sidecar(s): ${orphan.join(', ')}` : '');
      messageApi.success(msg);
      onUploaded();
    } catch (e) {
      messageApi.error((e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  return (
    <>
      {contextHolder}
      <Upload.Dragger
        multiple
        fileList={fileList}
        beforeUpload={(f) => {
          setFileList((prev) => [...prev, { uid: f.uid, name: f.name, originFileObj: f }]);
          return false;
        }}
        onRemove={(f) => setFileList((prev) => prev.filter((x) => x.uid !== f.uid))}
      >
        <p className="ant-upload-drag-icon"><InboxOutlined /></p>
        <p className="ant-upload-text">Click or drag files here</p>
        <p className="ant-upload-hint">Multiple files supported; the same optional meta.toml applies to each.</p>
      </Upload.Dragger>
      <Input.TextArea
        rows={3}
        placeholder=".meta.toml (optional)"
        value={meta}
        onChange={(e) => setMeta(e.target.value)}
        style={{ marginTop: 12 }}
      />
      <Button
        type="primary"
        icon={<UploadOutlined />}
        onClick={submit}
        disabled={fileList.length === 0 || busy}
        loading={busy}
        block
        style={{ marginTop: 12 }}
      >
        Upload{fileList.length > 0 ? ` (${fileList.length})` : ''}
      </Button>
    </>
  );
}
