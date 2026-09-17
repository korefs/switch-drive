const supported = ['pt-BR', 'en-US'];
const initial = navigator.language?.toLowerCase().startsWith('pt') ? 'pt-BR' : 'en-US';

function setLanguage(language) {
  const selected = supported.includes(language) ? language : 'en-US';
  document.documentElement.lang = selected;
  document.querySelectorAll('[data-set-language]').forEach(button => {
    button.setAttribute('aria-pressed', String(button.dataset.setLanguage === selected));
  });
  const title = document.querySelector(`[data-title-${selected === 'pt-BR' ? 'pt' : 'en'}]`);
  if (title) document.title = title.getAttribute(`data-title-${selected === 'pt-BR' ? 'pt' : 'en'}`) ?? document.title;
}

document.querySelectorAll('[data-set-language]').forEach(button => {
  button.addEventListener('click', () => setLanguage(button.dataset.setLanguage));
});

setLanguage(initial);
