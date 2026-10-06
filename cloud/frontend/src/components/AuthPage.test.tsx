import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import AuthPage from './AuthPage';
import { login, register } from '../api';

vi.mock('../api', () => ({
  login: vi.fn(),
  register: vi.fn(),
  verifyEmail: vi.fn(),
  resendCode: vi.fn(),
  forgotPassword: vi.fn(),
  resetPassword: vi.fn(),
}));

const mockLogin = login as unknown as ReturnType<typeof vi.fn>;
const mockRegister = register as unknown as ReturnType<typeof vi.fn>;

describe('AuthPage', () => {
  beforeEach(() => {
    mockLogin.mockReset();
    mockRegister.mockReset();
  });

  it('logs in and calls onLogin with token and username', async () => {
    mockLogin.mockResolvedValue({ token: 'tok', username: 'alice', email: 'a@b.co' });
    const onLogin = vi.fn();
    render(<AuthPage onLogin={onLogin} />);

    fireEvent.change(screen.getByPlaceholderText('Email or username'), { target: { value: 'alice' } });
    fireEvent.change(screen.getByPlaceholderText('Password'), { target: { value: 'pw' } });
    fireEvent.click(screen.getByRole('button', { name: /sign in/i }));

    await waitFor(() => expect(mockLogin).toHaveBeenCalledWith('alice', 'pw'));
    await waitFor(() => expect(onLogin).toHaveBeenCalledWith('tok', 'alice'));
  });

  it('registers and shows the verify step', async () => {
    mockRegister.mockResolvedValue({ username: 'bob', email: 'bob@example.org', verified: false });
    render(<AuthPage onLogin={vi.fn()} />);

    fireEvent.click(screen.getByRole('tab', { name: /register/i }));
    fireEvent.change(screen.getByPlaceholderText('Username'), { target: { value: 'bob' } });
    fireEvent.change(screen.getByPlaceholderText('Email'), { target: { value: 'bob@example.org' } });
    fireEvent.change(screen.getAllByPlaceholderText('Password').pop()!, { target: { value: 'password1' } });
    fireEvent.change(screen.getByPlaceholderText('Confirm password'), { target: { value: 'password1' } });
    fireEvent.click(screen.getByRole('button', { name: /create account/i }));

    await waitFor(() =>
      expect(mockRegister).toHaveBeenCalledWith(
        expect.objectContaining({ username: 'bob', email: 'bob@example.org' }),
      ),
    );
    await waitFor(() => expect(screen.getByRole('button', { name: /verify/i })).toBeInTheDocument());
  });

  it('shows an error on failed login', async () => {
    mockLogin.mockRejectedValue(new Error('bad credentials'));
    render(<AuthPage onLogin={vi.fn()} />);

    fireEvent.change(screen.getByPlaceholderText('Email or username'), { target: { value: 'alice' } });
    fireEvent.change(screen.getByPlaceholderText('Password'), { target: { value: 'x' } });
    fireEvent.click(screen.getByRole('button', { name: /sign in/i }));

    expect(await screen.findByText('bad credentials')).toBeInTheDocument();
  });
});
