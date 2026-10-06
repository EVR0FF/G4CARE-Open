//==============================================================================
// G4CARE cloud — email sending (SMTP in production, console in dev/demo).
//==============================================================================

import nodemailer from 'nodemailer';

export interface EmailSender {
  send(to: string, subject: string, text: string): Promise<void>;
}

export interface SmtpConfig {
  host: string;
  port: number;
  secure: boolean;
  user?: string;
  pass?: string;
  from: string;
}

export class SmtpSender implements EmailSender {
  private readonly transporter: ReturnType<typeof nodemailer.createTransport>;
  private readonly from: string;

  constructor(cfg: SmtpConfig) {
    this.from = cfg.from;
    this.transporter = nodemailer.createTransport({
      host: cfg.host,
      port: cfg.port,
      secure: cfg.secure,
      auth: cfg.user ? { user: cfg.user, pass: cfg.pass } : undefined,
    });
  }

  async send(to: string, subject: string, text: string): Promise<void> {
    await this.transporter.sendMail({ from: this.from, to, subject, text });
  }

  /// Check the SMTP connection + credentials (logs result; never throws).
  async verify(): Promise<void> {
    try {
      await this.transporter.verify();
      console.log(`[mailer] SMTP connection OK — from ${this.from}`);
    } catch (e) {
      console.error(`[mailer] SMTP verify failed: ${(e as Error).message}`);
    }
  }
}

/// Logs the email instead of sending it — for local dev / demos without SMTP.
export class ConsoleSender implements EmailSender {
  async send(to: string, subject: string, text: string): Promise<void> {
    console.log(
      `\n── email (console sender) ──────────────────\nTo: ${to}\nSubject: ${subject}\n\n${text}\n────────────────────────────────────────────\n`,
    );
  }
}

/// Pick a sender from env: SMTP if SMTP_HOST is set, otherwise console.
export function mailerFromEnv(): EmailSender {
  const host = process.env.SMTP_HOST;
  if (host) {
    const from = process.env.SMTP_FROM ?? 'noreply@g4care.cloud';
    console.log(`[mailer] SMTP enabled: ${host}:${process.env.SMTP_PORT ?? 587} (from ${from})`);
    const sender = new SmtpSender({
      host,
      port: Number(process.env.SMTP_PORT ?? 587),
      secure: process.env.SMTP_SECURE === 'true',
      user: process.env.SMTP_USER,
      pass: process.env.SMTP_PASS,
      from,
    });
    void sender.verify();
    return sender;
  }
  console.warn('[mailer] SMTP_HOST is not set — using console sender: emails are only logged, NOT sent.');
  return new ConsoleSender();
}
