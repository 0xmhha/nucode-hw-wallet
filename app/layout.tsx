import type { Metadata } from 'next';
import { Geist, Geist_Mono } from 'next/font/google';
import './globals.css';

const geistSans = Geist({
  variable: '--font-geist-sans',
  subsets: ['latin'],
});

const geistMono = Geist_Mono({
  variable: '--font-geist-mono',
  subsets: ['latin'],
});

export const metadata: Metadata = {
  title: 'NuWallet — Ethereum & Solana Hardware Wallet',
  description: 'NU-40 DK 기반 Ethereum Sepolia 및 Solana Devnet 하드웨어 지갑 설정',
  openGraph: {
    title: 'NuWallet Setup',
    description: 'Ethereum Sepolia와 Solana Devnet 하드웨어 지갑',
    images: ['/og.png'],
  },
  twitter: {
    card: 'summary_large_image',
    title: 'NuWallet Setup',
    description: 'Ethereum Sepolia와 Solana Devnet 하드웨어 지갑',
    images: ['/og.png'],
  },
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="ko">
      <body
        className={`${geistSans.variable} ${geistMono.variable} antialiased`}
      >
        {children}
      </body>
    </html>
  );
}
