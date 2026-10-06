import { useState } from 'react';
import { Alert, Button, Card, DatePicker, Form, Input, Space, Tabs, Typography, message } from 'antd';
import { LockOutlined, MailOutlined, UserOutlined } from '@ant-design/icons';
import type { Dayjs } from 'dayjs';
import { login, register, verifyEmail, resendCode, forgotPassword, resetPassword } from '../api';

const { Title, Text, Link } = Typography;

type Step = 'login' | 'register' | 'verify' | 'forgot' | 'reset';

export default function AuthPage({ onLogin }: { onLogin: (token: string, username: string) => void }) {
  const [step, setStep] = useState<Step>('login');
  const [email, setEmail] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [messageApi, contextHolder] = message.useMessage();

  const fail = (e: unknown) => setError((e as Error).message);

  const doLogin = async (v: { login: string; password: string }) => {
    setBusy(true); setError('');
    try {
      const res = await login(v.login, v.password);
      onLogin(res.token, res.username);
    } catch (e) { fail(e); } finally { setBusy(false); }
  };

  const doRegister = async (v: {
    username: string; email: string; password: string; confirm?: string;
    lastName?: string; firstName?: string; middleName?: string;
    phone?: string; birthDate?: Dayjs; institute?: string; group?: string;
  }) => {
    setBusy(true); setError('');
    try {
      const fullName = [v.lastName, v.firstName, v.middleName].filter(Boolean).join(' ').trim();
      await register({
        username: v.username,
        email: v.email,
        password: v.password,
        fullName: fullName || undefined,
        phone: v.phone || undefined,
        birthDate: v.birthDate ? v.birthDate.format('YYYY-MM-DD') : undefined,
        institute: v.institute || undefined,
        group: v.group || undefined,
      });
      setEmail(v.email);
      setStep('verify');
      messageApi.success(`Verification code sent to ${v.email}`);
    } catch (e) { fail(e); } finally { setBusy(false); }
  };

  const doVerify = async (v: { code: string }) => {
    setBusy(true); setError('');
    try {
      await verifyEmail(email, v.code);
      messageApi.success('Email verified — please sign in');
      setStep('login');
    } catch (e) { fail(e); } finally { setBusy(false); }
  };

  const doResend = async () => {
    try { await resendCode(email); messageApi.info('Code re-sent'); }
    catch (e) { fail(e); }
  };

  const doForgot = async (v: { email: string }) => {
    setBusy(true); setError('');
    try {
      await forgotPassword(v.email);
      setEmail(v.email);
      setStep('reset');
      messageApi.success('If the email exists, a reset code was sent');
    } catch (e) { fail(e); } finally { setBusy(false); }
  };

  const doReset = async (v: { code: string; newPassword: string }) => {
    setBusy(true); setError('');
    try {
      await resetPassword(email, v.code, v.newPassword);
      messageApi.success('Password reset — please sign in');
      setStep('login');
    } catch (e) { fail(e); } finally { setBusy(false); }
  };

  const loginForm = (
    <Form layout="vertical" onFinish={doLogin} requiredMark={false}>
      <Form.Item name="login" rules={[{ required: true, message: 'Email or username is required' }]}>
        <Input prefix={<UserOutlined />} placeholder="Email or username" autoComplete="username" size="large" />
      </Form.Item>
      <Form.Item name="password" rules={[{ required: true, message: 'Password is required' }]}>
        <Input.Password prefix={<LockOutlined />} placeholder="Password" autoComplete="current-password" size="large" />
      </Form.Item>
      <Form.Item style={{ marginBottom: 8 }}>
        <Button type="primary" htmlType="submit" block size="large" loading={busy}>Sign in</Button>
      </Form.Item>
      <div style={{ textAlign: 'center' }}>
        <Link onClick={() => { setError(''); setStep('forgot'); }}>Forgot password?</Link>
      </div>
    </Form>
  );

  const registerForm = (
    <Form layout="vertical" onFinish={doRegister} requiredMark={false}>
      <Form.Item name="username" rules={[{ required: true, message: 'Username is required' }]}>
        <Input prefix={<UserOutlined />} placeholder="Username" size="large" />
      </Form.Item>
      <Form.Item name="email" rules={[{ required: true, type: 'email', message: 'A valid email is required' }]}>
        <Input prefix={<MailOutlined />} placeholder="Email" size="large" />
      </Form.Item>
      <Form.Item name="password" rules={[{ required: true, min: 8, message: 'At least 8 characters' }]}>
        <Input.Password prefix={<LockOutlined />} placeholder="Password" autoComplete="new-password" size="large" />
      </Form.Item>
      <Form.Item
        name="confirm"
        dependencies={['password']}
        rules={[
          { required: true, message: 'Please confirm your password' },
          ({ getFieldValue }) => ({
            validator: (_, value) => {
              if (!value || getFieldValue('password') === value) return Promise.resolve();
              return Promise.reject(new Error('Passwords do not match'));
            },
          }),
        ]}
      >
        <Input.Password prefix={<LockOutlined />} placeholder="Confirm password" autoComplete="new-password" size="large" />
      </Form.Item>
      <Form.Item name="lastName"><Input placeholder="Surname (optional)" size="large" /></Form.Item>
      <Form.Item name="firstName"><Input placeholder="First name (optional)" size="large" /></Form.Item>
      <Form.Item name="middleName"><Input placeholder="Patronymic (optional)" size="large" /></Form.Item>
      <Form.Item name="phone"><Input placeholder="Phone (optional)" size="large" /></Form.Item>
      <Form.Item name="birthDate"><DatePicker placeholder="Date of birth (optional)" style={{ width: '100%' }} size="large" /></Form.Item>
      <Form.Item name="institute"><Input placeholder="Institute (optional)" size="large" /></Form.Item>
      <Form.Item name="group"><Input placeholder="Group (optional)" size="large" /></Form.Item>
      <Form.Item style={{ marginBottom: 0 }}>
        <Button type="primary" htmlType="submit" block size="large" loading={busy}>Create account</Button>
      </Form.Item>
    </Form>
  );

  return (
    <div className="auth">
      <Card className="auth-card" styles={{ body: { padding: 32 } }}>
        {contextHolder}
        <div style={{ textAlign: 'center', marginBottom: 16 }}>
          <Title level={3} style={{ margin: 0 }}>G4CARE Cloud</Title>
          <Text type="secondary">Share and browse detector artifacts</Text>
        </div>

        {error && <Alert type="error" message={error} showIcon closable onClose={() => setError('')} style={{ marginBottom: 16 }} />}

        {step === 'login' || step === 'register' ? (
          <Tabs
            centered
            activeKey={step}
            onChange={(k) => { setError(''); setStep(k as Step); }}
            items={[
              { key: 'login', label: 'Sign in', children: loginForm },
              { key: 'register', label: 'Register', children: registerForm },
            ]}
          />
        ) : step === 'verify' ? (
          <Form layout="vertical" onFinish={doVerify} requiredMark={false}>
            <Text>We sent a 6-digit code to <b>{email}</b>. Enter it below.</Text>
            <Form.Item name="code" rules={[{ required: true, len: 6, message: 'Enter the 6-digit code' }]} style={{ marginTop: 16 }}>
              <Input.OTP length={6} size="large" />
            </Form.Item>
            <Form.Item style={{ marginBottom: 8 }}>
              <Button type="primary" htmlType="submit" block size="large" loading={busy}>Verify</Button>
            </Form.Item>
            <Space style={{ width: '100%', justifyContent: 'center' }}>
              <Button type="link" onClick={doResend}>Resend code</Button>
              <Button type="link" onClick={() => { setError(''); setStep('login'); }}>Back to sign in</Button>
            </Space>
          </Form>
        ) : step === 'forgot' ? (
          <Form layout="vertical" onFinish={doForgot} requiredMark={false}>
            <Form.Item name="email" rules={[{ required: true, type: 'email', message: 'A valid email is required' }]}>
              <Input prefix={<MailOutlined />} placeholder="Your account email" size="large" />
            </Form.Item>
            <Form.Item style={{ marginBottom: 8 }}>
              <Button type="primary" htmlType="submit" block size="large" loading={busy}>Send reset code</Button>
            </Form.Item>
            <div style={{ textAlign: 'center' }}>
              <Button type="link" onClick={() => { setError(''); setStep('login'); }}>Back to sign in</Button>
            </div>
          </Form>
        ) : (
          <Form layout="vertical" onFinish={doReset} requiredMark={false}>
            <Form.Item name="code" rules={[{ required: true, len: 6, message: 'Enter the 6-digit code' }]}>
              <Input.OTP length={6} size="large" />
            </Form.Item>
            <Form.Item name="newPassword" rules={[{ required: true, min: 8, message: 'At least 8 characters' }]}>
              <Input.Password prefix={<LockOutlined />} placeholder="New password" autoComplete="new-password" size="large" />
            </Form.Item>
            <Form.Item style={{ marginBottom: 8 }}>
              <Button type="primary" htmlType="submit" block size="large" loading={busy}>Reset password</Button>
            </Form.Item>
            <div style={{ textAlign: 'center' }}>
              <Button type="link" onClick={() => { setError(''); setStep('login'); }}>Back to sign in</Button>
            </div>
          </Form>
        )}
      </Card>
    </div>
  );
}
