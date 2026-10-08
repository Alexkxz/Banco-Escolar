import type { ButtonHTMLAttributes, HTMLAttributes, ReactNode } from 'react'
import { Inbox } from 'lucide-react'
import { DevelopmentBadge } from './DevelopmentBadge'

export function Button({ variant = 'secondary', className = '', ...props }: ButtonHTMLAttributes<HTMLButtonElement> & { variant?: 'primary' | 'secondary' | 'quiet' }) {
  return <button className={`button button-${variant} ${className}`} {...props} />
}

export function Card({ className = '', ...props }: HTMLAttributes<HTMLElement>) {
  return <section className={`card ${className}`} {...props} />
}

export function PageHeader({ eyebrow, title, description }: { eyebrow: string; title: string; description: string }) {
  return <div className="page-heading"><div><div className="eyebrow">{eyebrow}</div><h1>{title}</h1><p>{description}</p></div><DevelopmentBadge /></div>
}

export function SectionHeader({ title, description, action }: { title: string; description?: string; action?: ReactNode }) {
  return <div className="section-header"><div><h2>{title}</h2>{description && <p>{description}</p>}</div>{action}</div>
}

export function StatusBadge({ children = 'En preparación' }: { children?: ReactNode }) {
  return <span className="status-badge"><span className="status-dot" />{children}</span>
}

export function EmptyState({ title = 'Todavía no hay información disponible', description = 'Esta área se completará en una fase posterior.', icon: Icon = Inbox, action }: { title?: string; description?: string; icon?: typeof Inbox; action?: ReactNode }) {
  return <div className="empty-state"><span className="empty-icon"><Icon size={23} strokeWidth={1.7} /></span><h3>{title}</h3><p>{description}</p>{action}</div>
}
