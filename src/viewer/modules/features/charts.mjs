import { createChartForm, mountChartForm } from '../chart-form.mjs';
import { decorateButton } from '../icons.mjs';
import { mountFeatureStyle } from './style.mjs';
export const chartsFeature = {
 id: 'charts',
 mount(shell) {
  const doc = shell.gridElement.ownerDocument;
  const removeStyle = mountFeatureStyle(doc, new URL('./charts.css', import.meta.url));
  doc.getElementById('viewer-tools').insertAdjacentHTML('beforeend', '<button id="add-chart" type="button">Charts</button>');
  doc.body.insertAdjacentHTML('beforeend', `
  <dialog id="chart-dirty-dialog" class="csvzall-dialog">
    <form id="chart-dirty-form" class="dialog-form">
      <h2>Unsaved changes</h2>
      <p>Save or discard current changes before opening Charts.</p>
      <div class="dialog-actions">
        <button id="cancel-chart-dirty" type="button">Cancel</button>
        <button id="discard-chart-dirty" type="button">Discard changes</button>
        <button id="save-chart-dirty" type="submit">Save changes</button>
      </div>
    </form>
  </dialog>
`);
  const button = doc.getElementById('add-chart');
  decorateButton(button,'chart-bar','Charts');
  const dialog = doc.createElement('dialog'); dialog.id = 'heatmap-chart-dialog'; dialog.className = 'csvzall-dialog chart-dialog';
  doc.body.append(dialog); createChartForm(dialog);
  const schema = shell.schema;
  const form = mountChartForm(dialog, { schema, fetchJson: (...args) => shell.backend.fetchJson(...args), postJson: (...args) => shell.backend.postJson(...args), decorateButton, close: value => dialog.close(value), status: shell.setStatus });
  const dirtyDialog = doc.getElementById('chart-dirty-dialog');
  const dirtyForm = doc.getElementById('chart-dirty-form');
  const cancel = doc.getElementById('cancel-chart-dirty');
  const discard = doc.getElementById('discard-chart-dirty');
  dirtyForm.onsubmit = event => { event.preventDefault(); dirtyDialog.close('save'); };
  cancel.onclick = () => dirtyDialog.close('cancel');
  discard.onclick = () => dirtyDialog.close('discard');
  let opening = false;
  const open = async () => {
    if (opening) return; opening = true;
    try {
      await shell.finishEditing();
      if (shell.dirty) {
        const action = await new Promise(resolve => { dirtyDialog.addEventListener('close', () => resolve(dirtyDialog.returnValue), {once:true}); dirtyDialog.showModal(); });
        if (action === 'save') await shell.save();
        else if (action === 'discard') await shell.reset();
        else return;
        if (shell.dirty) return;
      }
      Object.assign(schema, shell.schema);
      dialog.showModal(); await form.open();
    } catch (error) { shell.setStatus(error.message); }
    finally { opening = false; }
  };
  button.addEventListener('click',open);
  return { destroy() { removeStyle(); button.removeEventListener('click',open); form.dispose(); dialog.remove(); dirtyDialog.remove(); button.remove(); } };
 },
};
