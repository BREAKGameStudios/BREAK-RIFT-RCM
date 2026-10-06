# Protocol compatibility tests

Os testes validam exemplos aceitos e rejeitados pelos schemas normativos do RIFT Local Protocol v1.

```powershell
python -m pip install -r tests/protocol/requirements.txt
python tests/protocol/validate.py
```

Um caso inválido passa quando o schema o rejeita. Adicione todo campo ou regra nova primeiro aos schemas e depois inclua ao menos um exemplo válido e um inválido relevante.
