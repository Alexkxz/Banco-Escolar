export const MAX_STUDENT_IMAGE_BYTES = 5 * 1024 * 1024
export const MAX_STUDENT_IMAGE_PIXELS = 25_000_000
export type ImageDecoder = (file: File) => Promise<{ width: number; height: number; close?: () => void }>

const expectedSignatures: Record<string, number[][]> = {
  'image/png': [[0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]],
  'image/jpeg': [[0xff, 0xd8, 0xff]],
}

async function decodeImage(file: File) {
  if (typeof createImageBitmap !== 'function') throw new Error('Este navegador no puede validar la decodificación de imágenes.')
  return createImageBitmap(file)
}

function readPrefix(file: File, length: number): Promise<Uint8Array> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader()
    reader.onload = () => resolve(new Uint8Array(reader.result as ArrayBuffer))
    reader.onerror = () => reject(new Error('No se pudo leer el archivo de imagen.'))
    reader.readAsArrayBuffer(file.slice(0, length))
  })
}

export async function validateStudentImage(file: File, decoder: ImageDecoder = decodeImage): Promise<{ width: number; height: number }> {
  const signatures = expectedSignatures[file.type]
  if (!signatures) throw new Error('Elige un archivo PNG o JPEG.')
  if (file.size <= 0 || file.size > MAX_STUDENT_IMAGE_BYTES) throw new Error('La imagen debe pesar entre 1 byte y 5 MiB.')
  const bytes = await readPrefix(file, 8)
  if (!signatures.some((signature) => signature.every((byte, index) => bytes[index] === byte))) throw new Error('El contenido no coincide con el tipo de imagen seleccionado.')
  let decoded: Awaited<ReturnType<ImageDecoder>> | undefined
  try {
    decoded = await decoder(file)
    if (!decoded.width || !decoded.height || decoded.width * decoded.height > MAX_STUDENT_IMAGE_PIXELS) throw new Error('La imagen no tiene dimensiones válidas o es demasiado grande para previsualizarse.')
    return { width: decoded.width, height: decoded.height }
  } catch (error) {
    if (error instanceof Error && error.message.startsWith('La imagen')) throw error
    throw new Error('No se pudo decodificar la imagen seleccionada.')
  } finally {
    decoded?.close?.()
  }
}
