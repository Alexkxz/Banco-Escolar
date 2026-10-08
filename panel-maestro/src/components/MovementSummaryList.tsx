import { ArrowDownRight, ArrowUpRight } from 'lucide-react'
import { Link } from 'react-router-dom'
import type { MovementListEntry } from '../services/movementQueries'
import { movementDateFormatter } from '../services/movementQueries'

export function MovementSummaryList({ entries, label = 'Movimientos' }: { entries: readonly MovementListEntry[]; label?: string }) {
  return <div className="movement-record-list" role="list" aria-label={label}>
    {entries.map(({ movement, student, claim, activity }) => {
      const incoming = movement.type === 'ENTRY'
      const Arrow = incoming ? ArrowUpRight : ArrowDownRight
      return <article className="movement-record-row" role="listitem" key={movement.id}>
        <div className="movement-record-main"><Link to={`/movimientos/${movement.id}`}><strong>{movement.reason}</strong></Link><span>{student?.name ?? `Alumno ${movement.student_id}`} · {incoming ? 'Entrada' : 'Salida'}</span></div>
        <strong className={`movement-amount ${incoming ? 'is-credit' : 'is-debit'}`}><Arrow size={15} />{Math.abs(movement.amount).toLocaleString('es-MX')} Áureos</strong>
        <time className="movement-date" dateTime={new Date(movement.timestamp * 1000).toISOString()}>{movementDateFormatter.format(movement.timestamp * 1000)}</time>
        {claim && <span className="movement-reference">{activity && <><Link to="/actividades">Actividad {activity.id}</Link><span> · </span></>}<Link to="/cobros">Cobro {claim.id}</Link></span>}
      </article>
    })}
  </div>
}
