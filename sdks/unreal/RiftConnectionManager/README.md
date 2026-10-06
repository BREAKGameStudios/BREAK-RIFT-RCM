# RIFT Connection Manager — Unreal Engine SDK

Plugin Runtime do RIFT Connection Manager para Unreal Engine 5.

## Estado

Versão inicial `0.1.0`. O módulo base está pronto para receber a implementação do RIFT Local Protocol v1.

## Instalação durante o desenvolvimento

Copie a pasta `RiftConnectionManager` para a pasta `Plugins` do projeto Unreal e ative o plugin no editor.

```text
SeuJogo/
  Plugins/
    RiftConnectionManager/
      RiftConnectionManager.uplugin
      Source/
```

Se a pasta `Plugins` não existir, crie-a. Depois gere novamente os arquivos do projeto e compile o jogo.

## API planejada

A classe pública principal será `URiftConnectionSubsystem`. O plugin também fornecerá componentes opcionais para autenticação multiplayer e validação server-side de game tickets.

O código privado existente no OVER•RUN: BLITZKRIEG servirá como referência comportamental e será substituído pela dependência deste plugin.
