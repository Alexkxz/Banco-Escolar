import { cleanup, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it } from 'vitest'
import { MemoryRouter, Route, Routes } from 'react-router-dom'
import { StudentProfilePage } from '../src/pages/StudentProfilePage'
import { StudentPhotoProvider } from '../src/students/StudentPhotoContext'
import { DemoPanelDataService } from '../src/services/DemoPanelDataService'

afterEach(cleanup)
function renderProfile(service: DemoPanelDataService, id = 201) {
  return render(<MemoryRouter initialEntries={[`/alumnos/${id}`]}><StudentPhotoProvider><Routes><Route path="/alumnos/:studentId" element={<StudentProfilePage dataService={service} />} /></Routes></StudentPhotoProvider></MemoryRouter>)
}

describe('PM.7A ajustes desde el perfil', () => {
  it('presenta el saldo cero como cuenta y permite cancelar sin mutar datos', async () => {
    const service = new DemoPanelDataService()
    await service.adjustDemoAccount({ studentId: 201, operation: 'WITHDRAW', amount: 125, reason: '', expectedBalance: 125, confirmationId: 'zero' })
    renderProfile(service)
    await screen.findByText('Saldo actual')
    expect(document.querySelector('.profile-balance-value strong')).toHaveTextContent('0 Áureos')
    const count = (await service.getMovements()).length
    fireEvent.click(screen.getByRole('button', { name: 'Agregar Áureos' }))
    fireEvent.change(screen.getByLabelText('Cantidad de Áureos'), { target: { value: '5' } })
    fireEvent.click(screen.getByRole('button', { name: 'Revisar ajuste' }))
    expect(screen.getByRole('region', { name: 'Confirmación del ajuste' })).toHaveTextContent('5 Áureos')
    fireEvent.click(screen.getByRole('button', { name: 'Cancelar' }))
    expect((await service.getMovements()).length).toBe(count)
    expect((await service.getAccounts()).find((account) => account.student_id === 201)?.balance).toBe(0)
  })

  it('valida la cantidad, muestra advertencia negativa y confirma con saldo actualizado si cambió', async () => {
    const service = new DemoPanelDataService()
    renderProfile(service)
    await screen.findByRole('heading', { name: 'Ximena Sol' })
    fireEvent.click(screen.getByRole('button', { name: 'Retirar Áureos' }))
    const input = screen.getByLabelText('Cantidad de Áureos')
    fireEvent.change(input, { target: { value: '1.5' } })
    fireEvent.click(screen.getByRole('button', { name: 'Revisar ajuste' }))
    expect(screen.getByRole('alert')).toHaveTextContent('entera positiva')
    fireEvent.change(input, { target: { value: '130' } })
    fireEvent.click(screen.getByRole('button', { name: 'Revisar ajuste' }))
    expect(screen.getByRole('alert')).toHaveTextContent('será negativo')
    await service.adjustDemoAccount({ studentId: 201, operation: 'ADD', amount: 5, reason: '', expectedBalance: 125, confirmationId: 'concurrent' })
    fireEvent.click(screen.getByRole('button', { name: 'Confirmar ajuste' }))
    expect(await screen.findByText(/El saldo cambió/)).toBeInTheDocument()
    expect(document.querySelector('.profile-balance-value strong')).toHaveTextContent('130 Áureos')
    fireEvent.click(screen.getByRole('button', { name: 'Revisar ajuste' }))
    expect(screen.getByRole('region', { name: 'Confirmación del ajuste' })).toHaveTextContent('0 Áureos')
    fireEvent.click(screen.getByRole('button', { name: 'Confirmar ajuste' }))
    expect(await screen.findByRole('status')).toHaveTextContent('Ajuste aplicado')
    expect((await service.getAccounts()).find((account) => account.student_id === 201)?.balance).toBe(0)
  })

  it('muestra Sin cuenta y no presenta controles de ajuste', async () => {
    renderProfile(new DemoPanelDataService(), 206)
    expect((await screen.findAllByText('Sin cuenta')).length).toBeGreaterThanOrEqual(1)
    expect(screen.queryByRole('button', { name: 'Agregar Áureos' })).not.toBeInTheDocument()
    expect(screen.queryByRole('button', { name: 'Retirar Áureos' })).not.toBeInTheDocument()
  })

  it('conserva el formulario y no muestra éxito cuando falla la operación', async () => {
    const service = new DemoPanelDataService(() => Date.now(), (operation) => { if (operation === 'MANUAL_ACCOUNT_ADJUSTMENT') throw new Error('fallo de prueba') })
    renderProfile(service)
    await screen.findByRole('heading', { name: 'Ximena Sol' })
    fireEvent.click(screen.getByRole('button', { name: 'Agregar Áureos' }))
    fireEvent.change(screen.getByLabelText('Cantidad de Áureos'), { target: { value: '7' } })
    fireEvent.change(screen.getByLabelText('Motivo (opcional)'), { target: { value: 'Prueba' } })
    fireEvent.click(screen.getByRole('button', { name: 'Revisar ajuste' }))
    fireEvent.click(screen.getByRole('button', { name: 'Confirmar ajuste' }))
    expect(await screen.findByText(/No se aplicó el ajuste/)).toBeInTheDocument()
    expect(screen.getByLabelText('Cantidad de Áureos')).toHaveValue('7')
    expect(screen.getByLabelText('Motivo (opcional)')).toHaveValue('Prueba')
    expect(screen.queryByText(/Ajuste aplicado/)).not.toBeInTheDocument()
    expect((await service.getAccounts()).find(({ student_id }) => student_id === 201)?.balance).toBe(125)
  })
})
