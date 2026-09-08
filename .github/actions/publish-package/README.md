# Publish chef package action

Composite GitHub Action that publishes a built chef package with `order publish` using `CHEF_API_KEY` authentication.

## Usage

```yaml
steps:
  - name: Build and install chef
    id: chef
    uses: Meulengracht/bake/.github/actions/build-install@main

  - name: Publish package
    uses: Meulengracht/bake/.github/actions/publish-package@main
    with:
      api-key: ${{ secrets.CHEF_API_KEY }}
      order-path: ${{ steps.chef.outputs.install-prefix }}/bin/order
      package-path: path/to/package
      publisher: my-publisher
      channel: devel
```

The action invokes:

```bash
order publish <package-path> --publisher <publisher> --channel <channel>
```

## Inputs

| Name | Default | Description |
| --- | --- | --- |
| `api-key` | required | Chef API key used for publishing. Pass this from GitHub Secrets. |
| `package-path` | required | Path to the built chef package to publish. |
| `publisher` | required | Publisher name passed to `order publish`. |
| `channel` | `devel` | Channel passed to `order publish`. |
| `order-path` | `order` | Path to the `order` executable. |
| `working-directory` | `.` | Directory from which `order publish` is invoked. |
